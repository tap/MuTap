#!/usr/bin/env python3
"""kws_sources — the ingestion adapters: one per manifest `kind`.

An adapter lists an extracted archive as `kws_manifest.Clip` skeletons (id,
source, material, key, label, extra — the split, share, length and PCM
digest are the builder's to fill) plus the audio file to read, from the
directory `kws_store.Store.extract(source)` returns. Adapters never decode
audio and never draw: they read the archive's own lists and licence files and
refuse — by source and file, naming the rule — anything that does not match
the layout the manifest's `kind` promises. Layouts were read from the real
archives in the store on 9 September 2026 (rev 3 of the plan, §6 M4):

- `speech_commands_v2`  `<keyword>/<speakerhash>_nohash_<n>.wav` at the archive
  root (the upstream tarball prefixes members with `./`), `validation_list.txt`,
  `testing_list.txt`, `LICENSE`, `README.md`; `_background_noise_/` holds six
  long noise recordings and is excluded (it is neither keyword audio nor a
  MUSAN-style noise partition; the exclusion is recorded in the listing notes).
- `musan`  `musan/{music,noise,speech}/<subset>/*.wav`, a `LICENSE` per subset
  with `====`-separated attribution blocks that name file ids, an `ANNOTATIONS`
  per subset whose first column is the file id and, for music, whose fourth
  column is the artist (`id genres vocals artist [composer]`). Every clip gets
  its block as `extra.attribution` and a licence id classified from it as
  `extra.licence` (`musan_licence_id`), which the card's per-file NC check
  reads; a block that cannot be classified is refused. `options.exclude` lists
  file ids left out by the manifest — upstream's sound-bible LICENSE names 86
  of its 87 files, `noise-sound-bible-0000` having no block (read 9 Sept 2026).
- `openslr_28_simulated`  `RIRS_NOISES/simulated_rirs/{small,medium,large}room/
  Room*/Room*-*.wav` with a `rir_list` per room size
  (`--rir-id <id> --room-id <room> <path>`); only the simulated subset is read.
- `keyword_list`  a text file, one keyword per line (`options.member`), no audio.
- `piper`  a pinned voice archive (`<voice>.onnx` + `<voice>.onnx.json`, placed by hand and verified
  by `fetch` like every other source); it lists no clips — `synth` synthesizes them from
  `recipe.tts` — and the adapter only checks that the archive holds a voice model.
- `holdout`  M4c; refused here with a message that says so.
"""
from __future__ import annotations

import dataclasses
import pathlib
import re
from typing import Any, Callable

from kws_manifest import Clip, Recipe, Source
from kws_store import safe_member

SPEECH_COMMANDS_EXCLUDED = ("_background_noise_",)
SPEECH_COMMANDS_LISTS = {"validation_list.txt": "dev", "testing_list.txt": "eval"}
MUSAN_PARTITIONS = ("noise", "music", "speech")
SLR28_SIZES = ("small", "medium", "large")

_SPEECH_COMMANDS_NAME = re.compile(r"^(?P<speaker>[0-9a-f]{8})_nohash_(?P<n>\d+)\.wav$")
_SLR28_LINE = re.compile(r"^--rir-id\s+(?P<rir>\S+)\s+--room-id\s+(?P<room>\S+)\s+(?P<path>\S+)\s*$")


class SourceError(ValueError):
    """An archive that does not match its manifest `kind`; the message names the source, file and rule."""


@dataclasses.dataclass(frozen=True)
class Listed:
    """One clip an adapter found: its skeleton row and where its audio is (None for a keyword list)."""

    clip: Clip
    path: pathlib.Path | None
    relative: str                       # path without extension, relative to the extracted root
    official_split: str | None = None   # a split the source itself assigns (Speech Commands), else None


@dataclasses.dataclass
class Listing:
    clips: list[Listed]
    keywords: list[str] = dataclasses.field(default_factory=list)
    notes: list[str] = dataclasses.field(default_factory=list)


def clip_id(source: Source, relative: str) -> str:
    return f"{source.id}/{relative}"


def _skeleton(source: Source, relative: str, key: str, label: int | None, extra: dict[str, Any]) -> Clip:
    return Clip(id=clip_id(source, relative), source=source.id, material=source.material, split="", share="",
                label=label, endpoint_sample=None, length=0, key=key, pcm_sha256="", extra=extra)


# ---------------------------------------------------------------- speech_commands_v2


def list_speech_commands_v2(source: Source, root: pathlib.Path, recipe: Recipe) -> Listing:
    lists: dict[str, str] = {}
    for name, split in SPEECH_COMMANDS_LISTS.items():
        p = root / name
        if not p.exists():
            raise SourceError(f"source {source.id!r}: {name} missing at the archive root ({root}) — "
                              "speech_commands_v2 needs the official split lists")
        for line in p.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line:
                lists[line] = split
    if not (root / "LICENSE").exists():
        raise SourceError(f"source {source.id!r}: LICENSE missing at the archive root — refusing unlicensed "
                          "audio")
    want = source.options.get("keywords")
    if want is not None:
        want = set(want)
    near_miss = set(recipe.near_miss)
    out = Listing(clips=[])
    keyword_dirs = sorted(d for d in root.iterdir() if d.is_dir())
    for d in keyword_dirs:
        keyword = d.name
        if keyword in SPEECH_COMMANDS_EXCLUDED:
            n = len(list(d.glob("*.wav")))
            out.notes.append(f"{source.id}: excluded {keyword}/ ({n} files; not keyword audio)")
            continue
        if want is not None and keyword not in want:
            continue
        for wav in sorted(d.glob("*.wav")):
            m = _SPEECH_COMMANDS_NAME.match(wav.name)
            if m is None:
                raise SourceError(f"source {source.id!r}: {keyword}/{wav.name} does not match "
                                  "<speakerhash>_nohash_<n>.wav — cannot derive its speaker key")
            relative = f"{keyword}/{wav.stem}"
            official = lists.get(f"{keyword}/{wav.name}", "train")
            label = 1 if keyword == recipe.phrase else 0
            extra: dict[str, Any] = {"keyword": keyword}
            if label == 0 and keyword in near_miss:
                extra["near_miss"] = True
            skeleton = _skeleton(source, relative, m.group("speaker"), label, extra)
            out.clips.append(Listed(clip=skeleton, path=wav, relative=relative, official_split=official))
    if not out.clips:
        restricted = f" for keywords {sorted(want)}" if want is not None else ""
        raise SourceError(f"source {source.id!r}: no <keyword>/<speakerhash>_nohash_<n>.wav clips under "
                          f"{root}{restricted}")
    return out


# ---------------------------------------------------------------- musan


def _licence_blocks(text: str) -> list[str]:
    blocks = [b.strip() for b in re.split(r"^=+\s*$", text, flags=re.MULTILINE)]
    return [b for b in blocks if b]


_LICENCE_VERSION = re.compile(r"(?:attribution|licen[cs]e|\bcc\b|\bby\b)[^0-9]{0,40}?([1-4]\.[05])\b")


def musan_licence_id(block: str) -> str | None:
    """A licence id for one MUSAN attribution block (free-form text), or None when it cannot be classified.

    The card's licence checks read this from `extra.licence`, so a CC BY-NC track is refused per file
    rather than passing under the source's blanket CC BY 4.0; SA and ND ids are recorded per file so the
    card and ATTRIBUTION.csv state them accurately (whether such material is admitted corpus-wide is a
    plan-level policy, not the card's gate). Ids: `CC-BY[-<v>]`,
    `CC-BY-SA[-<v>]`, `CC-BY-NC[-<v>]` (ND folded in as `CC-BY-NC-ND` / `CC-BY-ND`), `CC0-1.0`,
    `public-domain`. Read against MUSAN's sound-bible, free-sound and fma LICENSE files, 9 Sept 2026.
    """
    t = " ".join(block.split()).lower()
    m = _LICENCE_VERSION.search(t)
    version = f"-{m.group(1)}" if m else ""
    nc = re.search(r"non-?commercial|by-nc|\bnc\b", t) is not None
    nd = re.search(r"no-?deriv|by-nd|\bnd\b", t) is not None
    sa = re.search(r"share-?alike|share alike|by-sa", t) is not None
    if nc:
        return "CC-BY-NC" + ("-SA" if sa else "-ND" if nd else "") + version
    if sa:
        return "CC-BY-SA" + version
    if nd:
        return "CC-BY-ND" + version
    if re.search(r"\bcc0\b|\bcc-0\b|creative commons 0|creative commons zero", t):
        return "CC0-1.0"
    if re.search(r"public domain", t):
        return "public-domain"
    if re.search(r"\bcc by\b|\bcc-by\b|\battribution\b", t):
        return "CC-BY" + version
    return None


def musan_attribution(licence_text: str, file_id: str) -> str | None:
    """The attribution block of a MUSAN subset LICENSE that names `file_id`, or the whole text when the file
    has no per-file block and the LICENSE is a single blanket statement; None when neither applies."""
    blocks = _licence_blocks(licence_text)
    pattern = re.compile(rf"(?<![\w-]){re.escape(file_id)}(?![\w-])")
    hits = [b for b in blocks if pattern.search(b)]
    if hits:
        return hits[0]
    if len(blocks) == 1 and not re.search(r"(?m)^(music|noise|speech)-[\w-]+-\d+\s*$", blocks[0]):
        return blocks[0]
    return None


def _read_annotations(path: pathlib.Path) -> dict[str, list[str]]:
    rows: dict[str, list[str]] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        cols = line.split()
        if cols:
            rows[cols[0]] = cols
    return rows


def list_musan(source: Source, root: pathlib.Path, recipe: Recipe) -> Listing:
    partition = source.options.get("partition")
    if partition not in MUSAN_PARTITIONS:
        raise SourceError(f"source {source.id!r}: options.partition must be one of {list(MUSAN_PARTITIONS)}, "
                          f"got {partition!r}")
    base = root / "musan" / partition
    if not base.is_dir():
        raise SourceError(f"source {source.id!r}: musan/{partition}/ missing under {root}")
    exclude = set(source.options.get("exclude", ()))  # file ids the manifest leaves out (e.g. unattributed)
    excluded: set[str] = set()
    out = Listing(clips=[])
    for subset in sorted(d for d in base.iterdir() if d.is_dir()):
        licence = subset / "LICENSE"
        if not licence.exists():
            raise SourceError(f"source {source.id!r}: musan/{partition}/{subset.name}/LICENSE missing — "
                              "MUSAN is attributed per file from the subset LICENSE")
        licence_text = licence.read_text(encoding="utf-8", errors="replace")
        annotations = _read_annotations(subset / "ANNOTATIONS") if (subset / "ANNOTATIONS").exists() else {}
        wavs = sorted(subset.glob("*.wav"))
        if not wavs:
            raise SourceError(f"source {source.id!r}: musan/{partition}/{subset.name}/ has no .wav files")
        for wav in wavs:
            file_id = wav.stem
            if file_id in exclude:
                out.notes.append(f"{source.id}: {file_id} excluded by options.exclude")
                excluded.add(file_id)
                continue
            attribution = musan_attribution(licence_text, file_id)
            if attribution is None:
                raise SourceError(f"source {source.id!r}: musan/{partition}/{subset.name}/LICENSE has no "
                                  f"attribution block naming {file_id} — every clip is attributed per file")
            licence_id = musan_licence_id(attribution)
            if licence_id is None:
                raise SourceError(f"source {source.id!r}: the LICENSE block for {file_id} cannot be "
                                  "classified as CC BY / CC BY-SA / CC BY-NC / CC0 / public domain — every "
                                  "clip carries a per-file licence id the card can check; exclude the file "
                                  "(options.exclude) or extend musan_licence_id")
            row = annotations.get(file_id)
            extra: dict[str, Any] = {"subset": subset.name, "attribution": attribution, "licence": licence_id}
            key = file_id
            if partition == "music":
                if row is not None and len(row) >= 4:
                    extra["artist"] = row[3]
                    key = row[3]
                    if len(row) >= 5:
                        extra["composer"] = row[4]
                else:
                    out.notes.append(f"{source.id}: {file_id} has no artist column in ANNOTATIONS; "
                                     "split key = file id")
            elif row is not None and len(row) >= 2:
                extra["annotation"] = " ".join(row[1:])
            relative = f"musan/{partition}/{subset.name}/{file_id}"
            label: int | None = 0 if partition in ("music", "speech") else None
            out.clips.append(Listed(clip=_skeleton(source, relative, key, label, extra), path=wav,
                                    relative=relative))
    if exclude - excluded:
        raise SourceError(f"source {source.id!r}: options.exclude names {sorted(exclude - excluded)}, not in "
                          f"musan/{partition}/")
    return out


# ---------------------------------------------------------------- openslr_28_simulated


def list_openslr_28_simulated(source: Source, root: pathlib.Path, recipe: Recipe) -> Listing:
    base = root / "RIRS_NOISES" / "simulated_rirs"
    if not base.is_dir():
        raise SourceError(f"source {source.id!r}: RIRS_NOISES/simulated_rirs/ missing under {root} — "
                          "only the simulated subset of SLR28 is ever read")
    sizes = source.options.get("sizes", list(SLR28_SIZES))
    bad = [s for s in sizes if s not in SLR28_SIZES]
    if bad:
        raise SourceError(f"source {source.id!r}: options.sizes {bad} not in {list(SLR28_SIZES)}")
    out = Listing(clips=[])
    for size in sizes:
        size_dir = base / f"{size}room"
        if not size_dir.is_dir():
            continue
        rir_list = size_dir / "rir_list"
        if not rir_list.exists():
            raise SourceError(f"source {source.id!r}: {size_dir.relative_to(root)}/rir_list missing — "
                              "SLR28 RIRs are keyed by the room id rir_list assigns")
        listed: dict[str, tuple[str, str]] = {}
        for line in rir_list.read_text(encoding="utf-8").splitlines():
            if not line.strip():
                continue
            m = _SLR28_LINE.match(line.strip())
            if m is None:
                raise SourceError(f"source {source.id!r}: rir_list line {line!r} is not "
                                  "'--rir-id <id> --room-id <room> <path>'")
            listed[m.group("path")] = (m.group("rir"), m.group("room"))
        wavs = sorted(size_dir.glob("Room*/Room*-*.wav"))
        if not wavs:
            raise SourceError(f"source {source.id!r}: no Room*/Room*-*.wav under "
                              f"{size_dir.relative_to(root)}")
        for wav in wavs:
            rel_path = wav.relative_to(root).as_posix()
            if rel_path not in listed:
                raise SourceError(f"source {source.id!r}: {rel_path} is not in {size}room/rir_list")
            rir_id, room_id = listed[rel_path]
            relative = rel_path[:-len(".wav")]
            extra = {"rir_id": rir_id, "room": room_id, "size": size}
            out.clips.append(Listed(clip=_skeleton(source, relative, room_id, None, extra), path=wav,
                                    relative=relative))
    if not out.clips:
        raise SourceError(f"source {source.id!r}: no simulated RIRs found for sizes {list(sizes)}")
    return out


# ---------------------------------------------------------------- keyword_list


def list_keyword_list(source: Source, root: pathlib.Path, recipe: Recipe) -> Listing:
    member = source.options.get("member")
    if not member:
        raise SourceError(f"source {source.id!r}: keyword_list needs options.member (the text file in the "
                          "archive)")
    if not safe_member(member) or not (root / member).resolve().is_relative_to(root.resolve()):
        raise SourceError(f"source {source.id!r}: options.member {member!r} must name a member of the "
                          "archive (relative, no '..')")
    p = root / member
    if not p.is_file():
        raise SourceError(f"source {source.id!r}: keyword list member {member!r} not in the archive")
    words = [w.strip() for w in p.read_text(encoding="utf-8").splitlines()]
    words = [w for w in words if w and not w.startswith("#")]
    if not words:
        raise SourceError(f"source {source.id!r}: keyword list {member!r} is empty")
    if len(set(words)) != len(words):
        dup = sorted({w for w in words if words.count(w) > 1})
        raise SourceError(f"source {source.id!r}: keyword list {member!r} repeats {dup}")
    return Listing(clips=[], keywords=words)


# ---------------------------------------------------------------- piper (a voice archive)


def list_piper(source: Source, root: pathlib.Path, recipe: Recipe) -> Listing:
    """A voice archive: no clips to decode (synth produces them); refuse an archive with no .onnx model."""
    models = sorted(p.relative_to(root).as_posix() for p in root.rglob("*.onnx"))
    if not models:
        raise SourceError(f"source {source.id!r}: kind 'piper' names a voice archive, but {root} holds no "
                          ".onnx model (voices are pinned .onnx + .onnx.json files)")
    return Listing(clips=[], notes=[f"{source.id}: voice archive ({', '.join(models)}); its clips are "
                                    "synthesized by `synth` from recipe.tts"])


# ---------------------------------------------------------------- holdout (M4c)


def list_holdout(source: Source, root: pathlib.Path, recipe: Recipe) -> Listing:
    raise SourceError(f"source {source.id!r}: kind 'holdout' is M4c — the recorded hold-out is not ingested "
                      "by this builder yet (its FLAC masters live under <store>/holdout/, verified against "
                      "holdout.json)")


ADAPTERS: dict[str, Callable[[Source, pathlib.Path, Recipe], Listing]] = {
    "speech_commands_v2": list_speech_commands_v2,
    "musan": list_musan,
    "openslr_28_simulated": list_openslr_28_simulated,
    "keyword_list": list_keyword_list,
    "piper": list_piper,
    "holdout": list_holdout,
}


def list_source(source: Source, root: pathlib.Path, recipe: Recipe) -> Listing:
    """Every clip of one extracted source, sorted by clip id, through the adapter its `kind` names."""
    try:
        adapter = ADAPTERS[source.kind]
    except KeyError:
        raise SourceError(f"source {source.id!r}: no ingestion adapter for kind {source.kind!r} "
                          f"(have {', '.join(ADAPTERS)})") from None
    listing = adapter(source, root, recipe)
    listing.clips.sort(key=lambda item: item.clip.id)
    ids = [item.clip.id for item in listing.clips]
    if len(set(ids)) != len(ids):
        dup = sorted({i for i in ids if ids.count(i) > 1})[:5]
        raise SourceError(f"source {source.id!r}: duplicate clip ids {dup}")
    return listing
