#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""make_toy_fixture — build tools/ml/kws/fixtures/toy/ from the store's verified upstream archives.

The toy fixture is the ≤ 1 MB, redistributable bring-up corpus that CI rebuilds
(wake-word plan §6 M4a): a Speech Commands v2 subset tarball (same layout as
upstream, the reduced validation/testing lists and the LICENSE), a MUSAN
excerpt tarball (2-second excerpts of noise and music files, each subset's
LICENSE block and ANNOTATIONS row retained), an SLR28 excerpt zip (two
simulated small-room RIRs — one landing in train, one in dev under the
builder's RIR split rule, both inside the recipe's RT60 range — and their
rir_list lines) and the Speech Commands keyword list, a
text file inside a tarball, because `Store.extract` (which the `keyword_list`
adapter reads through) only opens tar and zip archives. Never Common Voice.
Everything is chosen deterministically — sorted
candidates, fixed picks, fixed excerpt offsets — and the archives are written
byte-reproducibly (zero mtimes, no owner), so a re-run on the same upstream
archives yields the same sha256s that `manifest.json` records.

Upstream archives are read through `kws_store.Store` (verified against the
digests below, then extracted with member filtering); the script never
downloads. It runs on the maintainer's machine, not in CI:

    .venv/bin/python tools/ml/kws/fixtures/make_toy_fixture.py --store "$MUTAP_KWS_STORE" \\
        --musan-sha256 "$(cut -d' ' -f1 tools/ml/kws/fixtures/musan_sha256.txt)" --musan-size <bytes>

Per-file licence terms: MUSAN's noise and music subsets carry per-file terms
in free-form LICENSE files (some tracks are CC BY-SA, some CC BY-NC). Only
files whose LICENSE block reads as CC BY / CC0 / public domain are eligible
here; the block is copied into the toy tarball's LICENSE so the attribution
travels with the excerpt.
"""
from __future__ import annotations

import argparse
import dataclasses
import datetime
import gzip
import io
import json
import pathlib
import re
import sys
import tarfile
import zipfile

import numpy as np
import soundfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import kws_audio  # noqa: E402
from kws_build import rir_split  # noqa: E402
from kws_features import Geometry  # noqa: E402
from kws_manifest import (AugmentPolicy, Archive, LabelRule, Manifest, Origin, Recipe, Source,  # noqa: E402
                          SplitRule, load_manifest, manifest_hash, save_manifest)
from kws_sources import musan_licence_id  # noqa: E402
from kws_store import Store, resolve_store, sha256_file  # noqa: E402

REPO_ROOT = pathlib.Path(__file__).resolve().parents[4]
FIXTURE_DIR = pathlib.Path("tools/ml/kws/fixtures/toy")  # repository-relative: the manifest's origin.path
RATE = kws_audio.RATE
EXCERPT_SECONDS = 2.0
EXCERPT_OFFSET_SECONDS = 1.0  # a fixed offset into each MUSAN file; 0 when the file is shorter than 3 s
BUDGET_BYTES = 1_000_000     # "≤ 1 MB" of committed audio, read as the stricter decimal megabyte
TERMS_VERIFIED = "2026-09-08"
# The committed split salt. Any string works for the split rule; this one is dated so a later
# re-cut of the fixture that needs new picks can change it visibly (the picks depend on it).
SALT = "mutap-kws-toy-2026-09-09"
KEYWORDS_MEMBER = "speech_commands_keywords.txt"  # options.member of the keyword_list source

# The upstream archives, as verified on the M0 Mac 2026-09-09 (sha256 over the files downloaded from
# the URLs below). fetch() refuses anything else, so the toy set cannot be cut from a different
# release silently. MUSAN's digest and size are passed on the command line and recorded in
# ../musan_sha256.txt for M4b's manifest.
_SC_SHA256 = "af14739ee7dc311471de98f5f9d2c9191b18aedfe957f4a6ff791c709868ff58"
_SLR28_SHA256 = "3b50cfde915b3984738169b4beb341e9f6b8062ae4c2076146c5db71c2c05dc7"
UPSTREAM = {
    "speech_commands_v0.02": Source(
        id="speech_commands_v0.02", kind="speech_commands_v2", release="v0.02",
        origin=Origin(url="http://download.tensorflow.org/data/speech_commands_v0.02.tar.gz",
                      obtain="download the tarball into <store>/archives/speech_commands_v0.02/"),
        archive=Archive(file="speech_commands_v0.02.tar.gz", sha256=_SC_SHA256, size=2428923189),
        licence="CC-BY-4.0", attribution="Warden 2018", terms_accepted="CC BY 4.0",
        terms_verified=TERMS_VERIFIED, redistributable=True, roles=("train",), pcm_exact=True),
    "openslr_17_musan": Source(
        id="openslr_17_musan", kind="musan", release="SLR17",
        origin=Origin(url="https://www.openslr.org/resources/17/musan.tar.gz",
                      obtain="download the tarball into <store>/archives/openslr_17_musan/"),
        archive=Archive(file="musan.tar.gz", sha256="0" * 64, size=1),  # from --musan-sha256 / --musan-size
        licence="CC-BY-4.0", attribution="Snyder, Chen & Povey 2015", terms_accepted="CC BY 4.0",
        terms_verified=TERMS_VERIFIED, redistributable=True, roles=("aug",), pcm_exact=True,
        options={"partition": "noise"}),
    "openslr_28": Source(
        id="openslr_28", kind="openslr_28_simulated", release="SLR28",
        origin=Origin(url="https://www.openslr.org/resources/28/rirs_noises.zip",
                      obtain="download the zip into <store>/archives/openslr_28/"),
        archive=Archive(file="rirs_noises.zip", sha256=_SLR28_SHA256, size=1311166223),
        licence="Apache-2.0", attribution="Ko et al. 2017", terms_accepted="Apache License 2.0",
        terms_verified=TERMS_VERIFIED, redistributable=True, roles=("aug",), pcm_exact=True),
}

# Speech Commands picks: (keyword, {split: count}). marvin is the phrase, sheila the near miss; the
# official lists place every keyword in every split, so each split ends up with positives and negatives.
SPEECH_COMMANDS_PLAN = (
    ("marvin", {"train": 3, "dev": 2, "eval": 1}),
    ("sheila", {"train": 1, "dev": 1, "eval": 1}),
    ("yes", {"train": 1}),
    ("no", {"dev": 1}),
    ("left", {"eval": 1}),
    ("stop", {"train": 1}),
    ("right", {"train": 1}),
    ("go", {"eval": 1}),
)
# MUSAN picks per partition: how many excerpts each split needs (noise is keyed by file id, music by
# artist; the picks are the first sorted eligible files whose key lands in the split under SALT).
MUSAN_PLAN = {"noise": {"train": 2, "dev": 1, "eval": 1}, "music": {"train": 1, "eval": 1}}
FRACTIONS = SplitRule().fractions


# ---------------------------------------------------------------- reproducible archives


def _tar_gz(path: pathlib.Path, members: list[tuple[str, bytes]]) -> None:
    """A gzip tarball with zero mtimes, no owner and sorted members: the same bytes on every run."""
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w", format=tarfile.USTAR_FORMAT) as tf:
        for name, data in sorted(members):
            info = tarfile.TarInfo(name)
            info.size = len(data)
            info.mtime = 0
            info.mode = 0o644
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            tf.addfile(info, io.BytesIO(data))
    with path.open("wb") as f:
        with gzip.GzipFile(filename="", mode="wb", fileobj=f, mtime=0, compresslevel=9) as gz:
            gz.write(buf.getvalue())


def _zip(path: pathlib.Path, members: list[tuple[str, bytes]]) -> None:
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for name, data in sorted(members):
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            zf.writestr(info, data)


def _wav_bytes(x16: np.ndarray) -> bytes:
    buf = io.BytesIO()
    soundfile.write(buf, np.asarray(x16, dtype="<i2"), RATE, subtype="PCM_16", format="WAV")
    return buf.getvalue()


def _read_int16(path: pathlib.Path) -> np.ndarray:
    info = soundfile.info(str(path))
    if info.samplerate != RATE or info.subtype != "PCM_16" or info.channels != 1:
        raise RuntimeError(f"{path}: expected 16 kHz mono PCM_16, got {info.samplerate} Hz {info.subtype} "
                           f"x{info.channels} — the toy fixture keeps pcm_exact sources only")
    x16, _ = soundfile.read(str(path), dtype="int16", always_2d=True)
    return np.ascontiguousarray(x16[:, 0]).astype("<i2")


# ---------------------------------------------------------------- Speech Commands


@dataclasses.dataclass(frozen=True)
class SpeechCommandsPick:
    relpath: str   # <keyword>/<hash>_nohash_<n>.wav, as the official lists spell it
    split: str
    samples: int
    endpoint: int


def choose_speech_commands(root: pathlib.Path, plan=SPEECH_COMMANDS_PLAN) -> list[SpeechCommandsPick]:
    """The first sorted clips per (keyword, official split) that are a full second of audible speech.

    A clip qualifies when it is exactly 16000 samples (every split has plenty) and the trim rule finds
    an endpoint, so every positive carries a label.
    """
    validation = set(root.joinpath("validation_list.txt").read_text(encoding="utf-8").split())
    testing = set(root.joinpath("testing_list.txt").read_text(encoding="utf-8").split())
    rule = LabelRule()
    picks: list[SpeechCommandsPick] = []
    for keyword, wanted in plan:
        if not (root / keyword).is_dir():
            raise RuntimeError(f"speech_commands_v0.02: no keyword directory {keyword!r}")
        remaining = dict(wanted)
        for name in sorted(p.name for p in (root / keyword).glob("*.wav")):
            rel = f"{keyword}/{name}"
            split = "dev" if rel in validation else "eval" if rel in testing else "train"
            if remaining.get(split, 0) <= 0:
                continue
            x16 = _read_int16(root / rel)
            if x16.size != RATE:
                continue
            e = kws_audio.endpoint(kws_audio.from_int16(x16), rule.trim_db, rule.window_ms)
            if e is None:
                continue
            picks.append(SpeechCommandsPick(rel, split, int(x16.size), int(e)))
            remaining[split] -= 1
        short = {s: n for s, n in remaining.items() if n > 0}
        if short:
            raise RuntimeError(f"speech_commands_v0.02: could not fill {short} for keyword {keyword!r}")
    return picks


def build_speech_commands(root: pathlib.Path,
                          out: pathlib.Path) -> tuple[list[SpeechCommandsPick], list[str]]:
    """Write the Speech Commands toy tarball; returns the picks and the archive's keyword list."""
    picks = choose_speech_commands(root)
    chosen = {p.relpath for p in picks}
    members: list[tuple[str, bytes]] = []
    for p in picks:
        members.append((f"./{p.relpath}", (root / p.relpath).read_bytes()))
    for listing in ("validation_list.txt", "testing_list.txt"):
        kept = [line for line in root.joinpath(listing).read_text(encoding="utf-8").splitlines()
                    if line.strip() in chosen]
        members.append((f"./{listing}", ("\n".join(kept) + "\n").encode()))
    members.append(("./LICENSE", root.joinpath("LICENSE").read_bytes()))
    _tar_gz(out, members)
    keywords = sorted(p.name for p in root.iterdir() if p.is_dir() and p.name != "_background_noise_")
    return picks, keywords


# ---------------------------------------------------------------- MUSAN


_MUSAN_ID = re.compile(r"\b((?:noise|music|speech)-[a-z-]+?-\d{4})\b")
_BLOCK_SEP = re.compile(r"^=+\s*$", re.MULTILINE)


def eligible(licence_id: str | None) -> bool:
    """Only CC BY (any version), CC0 and public-domain files enter the toy set: never SA, NC or ND."""
    if licence_id in ("CC0-1.0", "public-domain"):
        return True
    return (licence_id is not None and licence_id.startswith("CC-BY")
            and not licence_id.startswith(("CC-BY-SA", "CC-BY-NC", "CC-BY-ND")))


def parse_licence_blocks(text: str) -> dict[str, tuple[str | None, str]]:
    """file id -> (licence id per kws_sources.musan_licence_id, the block's text) for every id a LICENSE
    file's blocks name."""
    out: dict[str, tuple[str | None, str]] = {}
    for block in _BLOCK_SEP.split(text):
        block = block.strip("\n")
        if not block.strip():
            continue
        licence_id = musan_licence_id(block)
        for fid in _MUSAN_ID.findall(block):
            out[fid] = (licence_id, block)
    return out


def parse_annotations(text: str) -> dict[str, str]:
    """file id -> its ANNOTATIONS row (music: `<id> <genres> <vocals> <artist> [<composer>]`)."""
    rows: dict[str, str] = {}
    for line in text.splitlines():
        if line.strip():
            rows[line.split()[0]] = line.rstrip()
    return rows


def musan_key(partition: str, fid: str, annotations: dict[str, str]) -> str:
    """The split key the musan adapter uses: file id, or the artist column of ANNOTATIONS for music."""
    if partition == "music" and fid in annotations:
        cols = annotations[fid].split()
        if len(cols) >= 4:
            return cols[3]
    return fid


@dataclasses.dataclass(frozen=True)
class MusanPick:
    partition: str
    subset: str
    fid: str
    split: str
    key: str
    licence: str
    offset: int  # excerpt start, samples


def choose_musan(root: pathlib.Path, salt: str,
                 plan=MUSAN_PLAN) -> tuple[list[MusanPick], list[tuple[str, bytes]]]:
    """Excerpts per partition and split; also the tar members (audio, LICENSE blocks, ANNOTATIONS rows)."""
    picks: list[MusanPick] = []
    members: list[tuple[str, bytes]] = []
    n = int(EXCERPT_SECONDS * RATE)
    lead = int(EXCERPT_OFFSET_SECONDS * RATE)
    for partition, wanted in plan.items():
        pdir = root / "musan" / partition
        if not pdir.is_dir():
            raise RuntimeError(f"musan: no partition directory {pdir}")
        remaining = dict(wanted)
        kept_licence: dict[str, list[str]] = {}
        kept_annotations: dict[str, list[str]] = {}
        for subset_dir in sorted(p for p in pdir.iterdir() if p.is_dir()):
            licence_text = (subset_dir / "LICENSE").read_text(encoding="utf-8", errors="replace")
            licence = parse_licence_blocks(licence_text)
            ann_path = subset_dir / "ANNOTATIONS"
            annotations = (parse_annotations(ann_path.read_text(encoding="utf-8", errors="replace"))
                           if ann_path.exists() else {})
            for wav in sorted(subset_dir.glob("*.wav")):
                fid = wav.stem
                cls, block = licence.get(fid, (None, ""))
                if not eligible(cls):
                    # BY-SA, BY-NC and files without a per-file block never enter the toy set. (The
                    # latter excludes noise/free-sound, whose LICENSE is one blanket public-domain
                    # statement naming no file; sound-bible attributes every file by title and author.)
                    continue
                if partition == "music" and fid not in annotations:
                    continue
                key = musan_key(partition, fid, annotations)
                split = kws_audio.assign_split(salt, key, FRACTIONS)
                if remaining.get(split, 0) <= 0:
                    continue
                x16 = _read_int16(wav)
                if x16.size < n:
                    continue
                offset = lead if x16.size >= n + lead else 0
                excerpt = x16[offset:offset + n]
                if not np.any(excerpt):
                    continue
                picks.append(MusanPick(partition, subset_dir.name, fid, split, key, cls, offset))
                members.append((f"musan/{partition}/{subset_dir.name}/{wav.name}", _wav_bytes(excerpt)))
                kept_licence.setdefault(subset_dir.name, []).append(block)
                if fid in annotations:
                    kept_annotations.setdefault(subset_dir.name, []).append(annotations[fid])
                remaining[split] -= 1
        short = {s: k for s, k in remaining.items() if k > 0}
        if short:
            raise RuntimeError(f"musan/{partition}: could not fill {short} with CC BY / public-domain files "
                               f"under salt {salt!r}")
        sep = "=" * 78 + "\n"
        for subset, blocks in kept_licence.items():
            text = sep.join(b + "\n" for b in dict.fromkeys(blocks))
            members.append((f"musan/{partition}/{subset}/LICENSE", text.encode()))
        for subset, rows in kept_annotations.items():
            members.append((f"musan/{partition}/{subset}/ANNOTATIONS", ("\n".join(rows) + "\n").encode()))
    return picks, members


# ---------------------------------------------------------------- SLR28


@dataclasses.dataclass(frozen=True)
class RirPick:
    relpath: str  # RIRS_NOISES/simulated_rirs/<set>room/<Room>/<Room>-<n>.wav
    room: str     # the rir_list --room-id, the adapter's split key (e.g. small-Room006)
    rir_list_line: str
    samples: int
    onset: int
    rt60_s: float


def choose_rirs(root: pathlib.Path, recipe: Recipe) -> list[RirPick]:
    """One small-room RIR per training-side split, train then dev: the first sorted RIR whose room —
    keyed by the `--room-id` of `rir_list` (`small-Room<n>`), as the openslr_28_simulated adapter keys
    it — lands in that split under the builder's RIR rule (`kws_build.rir_split`), and whose RT60
    (`kws_audio.rt60_t20`, the value the builder records) lies inside the recipe's `rt60_s` range.

    Positives in train and in dev each draw from a same-split RIR pool inside that range, so the toy
    needs both, and the augment stage refuses rather than falls back when a pool is empty. Measured
    9 Sept 2026 over the store's small rooms under the toy salt: Room001–Room004 are out of range
    (RT60 0.10–0.15 s), Room005 lands in dev (0.32 s), Room006 in train (0.27 s).
    """
    sim = root / "RIRS_NOISES" / "simulated_rirs"
    rir_list: dict[str, tuple[str, str]] = {}
    for line in (sim / "smallroom" / "rir_list").read_text(encoding="utf-8").splitlines():
        cols = line.split()
        if cols:
            rir_list[cols[-1]] = (cols[3], line.rstrip())  # --rir-id <id> --room-id <room> <path>
    lo, hi = recipe.augment.rt60_s
    picks: dict[str, RirPick] = {}
    for wav in sorted(sim.joinpath("smallroom").glob("Room*/Room*-*.wav")):
        rel = wav.relative_to(root).as_posix()
        if rel not in rir_list:
            raise RuntimeError(f"openslr_28: {rel} has no rir_list line")
        room, line = rir_list[rel]
        split = rir_split(recipe, room)
        if split in picks:
            continue
        h = kws_audio.from_int16(_read_int16(wav))
        rt60 = kws_audio.rt60_t20(h)
        if rt60 is None or not lo <= rt60 <= hi:
            continue
        picks[split] = RirPick(rel, room, line, int(h.size), kws_audio.rir_onset(h), rt60)
        if len(picks) == 2:
            break
    missing = [s for s in ("train", "dev") if s not in picks]
    if missing:
        raise RuntimeError(f"openslr_28: no small-room RIR inside rt60_s {list(recipe.augment.rt60_s)} lands "
                           f"in {missing} under salt {recipe.split.salt!r}")
    return [picks["train"], picks["dev"]]


def build_rirs(root: pathlib.Path, out: pathlib.Path, recipe: Recipe) -> list[RirPick]:
    picks = choose_rirs(root, recipe)
    readme = root / "RIRS_NOISES" / "simulated_rirs" / "README"
    members = [(p.relpath, (root / p.relpath).read_bytes()) for p in picks]
    lines = "".join(p.rir_list_line + "\n" for p in sorted(picks, key=lambda p: p.relpath))
    members += [("RIRS_NOISES/simulated_rirs/smallroom/rir_list", lines.encode()),
                ("RIRS_NOISES/simulated_rirs/README", readme.read_bytes())]
    _zip(out, members)
    return picks


# ---------------------------------------------------------------- the manifest


def _archive(path: pathlib.Path) -> Archive:
    return Archive(file=path.name, sha256=sha256_file(path), size=path.stat().st_size)


WARDEN = ("Warden, P. (2018). Speech Commands: A Dataset for Limited-Vocabulary Speech Recognition. "
          "arXiv:1804.03209. Creative Commons Attribution 4.0 International (CC BY 4.0)")
MUSAN = ("Snyder, D., Chen, G. & Povey, D. (2015). MUSAN: A Music, Speech, and Noise Corpus. "
         "arXiv:1510.08484. OpenSLR SLR17, CC BY 4.0; per-file terms in each subset's LICENSE, retained in "
         "the toy tarball")
KO = ("Ko, T., Peddinti, V., Povey, D., Seltzer, M. L. & Khudanpur, S. (2017). A study on data augmentation "
      "of reverberant speech for robust speech recognition. ICASSP 2017. OpenSLR SLR28, Apache License 2.0 "
      "(the release carries no LICENSE file; the licence is stated on the OpenSLR resource page)")


def toy_recipe(salt: str) -> Recipe:
    """The committed recipe: the reference geometry, the plain-log path, K = 2, M = 1, dry share 0.25 and
    otherwise kws_manifest's defaults (the RT60 range included — the RIRs are chosen to fit it)."""
    return Recipe(phrase="marvin", near_miss=("sheila",), geometry=Geometry(), stored_path="log",
                  log_mel_contract_version=1, context_s=2.0, split=SplitRule(salt=salt),
                  augment=AugmentPolicy(seed=1, draws_per_positive=2, noisy_per_negative=1, dry_share=0.25))


def toy_manifest(files: dict[str, pathlib.Path], sc_picks: list[SpeechCommandsPick],
                 musan_picks: list[MusanPick], rirs: list[RirPick], keywords: list[str], recipe: Recipe,
                 upstream_sources: dict[str, Source]) -> Manifest:
    """The committed manifest: one source per toy archive (MUSAN twice, once per partition) + the recipe."""
    origin = Origin(path=FIXTURE_DIR.as_posix())
    rir_text = ", ".join(f"{p.relpath} (RT60 {p.rt60_s:.3f} s)" for p in rirs)
    noise = [p for p in musan_picks if p.partition == "noise"]
    music = [p for p in musan_picks if p.partition == "music"]
    excerpt = f"{EXCERPT_SECONDS:g} s each from {EXCERPT_OFFSET_SECONDS:g} s in"
    upstream = {sid: s.archive.sha256 for sid, s in upstream_sources.items()}
    cc_by = "CC BY 4.0: attribution"
    sources = (
        Source(id="speech_commands_v0.02_toy", kind="speech_commands_v2", release="v0.02", origin=origin,
               archive=_archive(files["speech_commands"]), licence="CC-BY-4.0",
               attribution=f"{WARDEN}. Toy subset: {len(sc_picks)} clips, the reduced "
                           "validation/testing lists and the LICENSE, laid out as upstream",
               terms_accepted=cc_by, terms_verified=TERMS_VERIFIED, redistributable=True,
               roles=("train", "dev", "eval-speech"), pcm_exact=True,
               options={"upstream_sha256": upstream["speech_commands_v0.02"]}),
        Source(id="musan_noise_toy", kind="musan", release="SLR17", origin=origin,
               archive=_archive(files["musan"]), licence="CC-BY-4.0",
               attribution=f"{MUSAN}. Toy excerpts: {len(noise)} noise files, {excerpt}, CC BY / "
                           "public-domain files only",
               terms_accepted=cc_by, terms_verified=TERMS_VERIFIED, redistributable=True,
               roles=("aug", "eval-noise"), pcm_exact=True,
               options={"partition": "noise", "excerpt_s": EXCERPT_SECONDS,
                        "upstream_sha256": upstream["openslr_17_musan"]}),
        Source(id="musan_music_toy", kind="musan", release="SLR17", origin=origin,
               archive=_archive(files["musan"]), licence="CC-BY-4.0",
               attribution=f"{MUSAN}. Toy excerpts: {len(music)} music files, {excerpt}, CC BY / "
                           "public-domain tracks only, ANNOTATIONS rows retained for the artist key",
               terms_accepted=cc_by, terms_verified=TERMS_VERIFIED, redistributable=True,
               roles=("train", "eval-music"), pcm_exact=True,
               options={"partition": "music", "excerpt_s": EXCERPT_SECONDS,
                        "upstream_sha256": upstream["openslr_17_musan"]}),
        Source(id="openslr_28_toy", kind="openslr_28_simulated", release="SLR28", origin=origin,
               archive=_archive(files["rir"]), licence="Apache-2.0",
               attribution=f"{KO}. Toy excerpt: {len(rirs)} simulated small-room RIRs, one per training-side "
                           f"split, with their rir_list lines: {rir_text}",
               terms_accepted="Apache License 2.0", terms_verified=TERMS_VERIFIED, redistributable=True,
               roles=("aug",), pcm_exact=True, options={"upstream_sha256": upstream["openslr_28"]}),
        Source(id="speech_commands_keywords", kind="keyword_list", release="v0.02", origin=origin,
               archive=_archive(files["keywords"]), licence="CC-BY-4.0",
               attribution=f"The {len(keywords)} keywords of Speech Commands v0.02 ({WARDEN}), listed from "
                           "the archive's keyword directories (_background_noise_ excluded): the list the "
                           "training guide's toy mining run matches against, replaced by MSWC's at M4b",
               terms_accepted=cc_by, terms_verified=TERMS_VERIFIED, redistributable=True,
               roles=("train",), pcm_exact=False, options={"member": KEYWORDS_MEMBER}),
    )
    return Manifest(name="toy", sources=sources, recipe=recipe)


# ---------------------------------------------------------------- main


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--store", help="the feature store (or MUTAP_KWS_STORE)")
    ap.add_argument("--out", default=str(REPO_ROOT / FIXTURE_DIR), help="the fixture directory to write")
    ap.add_argument("--musan-sha256", required=True,
                    help="sha256 of the upstream musan.tar.gz (tools/ml/kws/fixtures/musan_sha256.txt)")
    ap.add_argument("--musan-size", type=int, required=True,
                    help="size in bytes of the upstream musan.tar.gz")
    ap.add_argument("--salt", default=SALT, help="the committed split salt")
    args = ap.parse_args(argv)

    store = Store(resolve_store(args.store))
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    upstream = dict(UPSTREAM)
    musan_archive = Archive(file="musan.tar.gz", sha256=args.musan_sha256, size=args.musan_size)
    upstream["openslr_17_musan"] = dataclasses.replace(upstream["openslr_17_musan"], archive=musan_archive)
    roots: dict[str, pathlib.Path] = {}
    for sid, src in upstream.items():
        store.fetch(src, REPO_ROOT)
        roots[sid] = store.extract(src)
        print(f"{sid}: verified and extracted at {roots[sid]}")

    files = {"speech_commands": out / "speech_commands_v0.02_toy.tar.gz", "musan": out / "musan_toy.tar.gz",
             "rir": out / "rirs_noises_toy.zip", "keywords": out / "speech_commands_keywords.tar.gz"}
    recipe = toy_recipe(args.salt)
    sc_picks, keywords = build_speech_commands(roots["speech_commands_v0.02"], files["speech_commands"])
    musan_picks, musan_members = choose_musan(roots["openslr_17_musan"], args.salt)
    _tar_gz(files["musan"], musan_members)
    rirs = build_rirs(roots["openslr_28"], files["rir"], recipe)
    _tar_gz(files["keywords"], [(KEYWORDS_MEMBER, ("\n".join(keywords) + "\n").encode())])

    archive_bytes = sum(p.stat().st_size for p in files.values())
    if archive_bytes > BUDGET_BYTES:
        raise SystemExit(f"toy fixture is {archive_bytes} bytes of archives, over the {BUDGET_BYTES} budget")

    m = toy_manifest(files, sc_picks, musan_picks, rirs, keywords, recipe, upstream)
    save_manifest(m, out / "manifest.json")
    loaded = load_manifest(out / "manifest.json")  # validate() refuses what the builder would refuse
    digest = manifest_hash(loaded)

    record = {
        "built": datetime.date.today().isoformat(), "salt": args.salt, "manifest_hash": digest,
        "archive_bytes": archive_bytes,
        "speech_commands": [dataclasses.asdict(p) for p in sc_picks],
        "musan": [dataclasses.asdict(p) for p in musan_picks],
        "rirs": [dataclasses.asdict(p) for p in rirs], "keywords": len(keywords),
        "upstream": {sid: {"sha256": s.archive.sha256, "size": s.archive.size}
                     for sid, s in upstream.items()},
    }
    (out / "provenance.json").write_text(json.dumps(record, indent=1) + "\n", encoding="utf-8")
    print(json.dumps({k: v for k, v in record.items() if k not in ("speech_commands", "musan")}, indent=1))
    print(f"{len(sc_picks)} Speech Commands clips, {len(musan_picks)} MUSAN excerpts, {len(rirs)} RIRs, "
          f"{len(keywords)} keywords; {archive_bytes} bytes of archives (budget {BUDGET_BYTES}); "
          f"manifest hash {digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
