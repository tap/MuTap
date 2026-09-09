#!/usr/bin/env python3
"""kws_build — rebuild a keyword-spotter dataset from its manifest, in stages, into the store.

    python3 tools/ml/kws/kws_build.py <stage> --manifest M --store DIR [--jobs N]
    stages: fetch · decode · synth · augment · extract · shard · all

The builder never downloads. Each stage reads what the previous one left under
`<store>/features/<manifest-hash>/` (`clips.json`, the dry and augmented rows)
and refuses to run out of order, on an unverified archive (every stage after
`fetch` re-checks every source's `.verified` marker), or under a front-end
contract version other than the one the manifest was written for. Every random
draw is `kws_audio.draw_rng(manifest seed, clip id, variant)`, resolved in the
main process in clip-id order before dispatch, so `lock.json` is byte-identical
whatever `--jobs` is (a process pool renders and featurizes; it never draws).

What a featurized row is (the rules the plan states, rev 3 §6 M4):
- train/dev positives: K = draws_per_positive variants (1..K), each a resolved
  draw — dry (gain only) with probability dry_share, else gain, speed, a same-
  split RIR inside the RT60 range, a same-split noise clip at an SNR draw;
- train/dev negatives (label 0): variant 0 clean plus M = noisy_per_negative
  draws; eval clips: variant 0 only, never augmented;
- every positive is featurized embedded in same-split negative material: a
  stream of drawn negative clips, the keyword replacing the stream from
  `offset` — drawn in [context_s, 1.5 * context_s] samples — for its own
  length, with (20 + T) hops plus one frame of the stream after it so M5's hit
  window [h - T, h + 20 + T] lies inside the rows; one `reset()` per mixture.
  The RIR and the SNR noise of a positive's draw are applied to the WHOLE
  mixture in `extract` (the keyword itself carries gain and speed only from
  `augment`), so the reverberation and the background run continuously across
  the context and the keyword — the condition the plan's augmentation exists
  for and the one M4c's hold-out realises by mixing noise into the whole room
  take — and no keyword-bounded burst marks the positives;
- negatives are featurized alone after a reset, their whole clip rendered
  (gain, speed, RIR, noise) by `augment`; noise and RIR material (label None)
  is never featurized, only mixed in.
The stored endpoint of a positive is e''' = round(e / speed) + rir_delay +
offset, never re-trimmed (the RIR's direct-path delay is the same whether the
RIR is applied to the keyword or to the mixture, since convolution commutes
with the splice); its hop index within the rows is e''' // hop, and extract
refuses a mixture whose rows do not reach it. `speed` in a draw is the
rational factor `kws_audio.change_speed` applies (Fraction(...).limit_denominator(1000)),
not the raw uniform draw, so the endpoint is computed from the factor the audio
underwent. In the lock a row's `endpoint_sample` is the endpoint inside the
audio that row describes: the dry e for a dry-only row, e''' for a featurized
one; `extra.endpoint_dry` always keeps e. A row's `length` is the sample count
of the audio its `pcm_sha256` digests (the dry clip, or the rendered variant
in the augmented tier); `extra.mixture_samples` is the mixture's. Lock rows
exist for every decoded clip (variant 0) and every draw.

Timing (measured 9 September 2026, M0 Mac, the committed toy fixture: 23
decoded clips — 15 Speech Commands, 2 music + 4 noise MUSAN 2 s excerpts,
2 SLR28 RIRs — 17 draws, 29 featurized rows, 3 shards): decode, augment and
extract each finish in under 0.1 s of work single-process; `all --jobs 1`
about 0.8 s wall, `all --jobs 4` about 2.5 s wall (three spawned pools each
importing scipy, soundfile and the bridge — a toy is faster single-process;
the pool pays off on the corpus). test_kws.py's docstring carries the exact
figures of the run that cut the committed expected outputs. The full-corpus
figures belong to M4b's Done record.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import dataclasses
import hashlib
import io
import json
import math
import multiprocessing
import os
import pathlib
import platform
import shutil
import subprocess
import sys
import time
import zipfile
from fractions import Fraction
from typing import Any, Callable

import numpy as np
import scipy
import soundfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import kws_audio  # noqa: E402
import kws_features  # noqa: E402
import kws_sources  # noqa: E402
from kws_manifest import (Clip, Lock, Manifest, ManifestError, Recipe, Shard, Source,  # noqa: E402
                          eval_set_id, load_manifest, manifest_hash, write_lock)
from kws_store import Store, StoreError, resolve_store, safe_member, sha256_file  # noqa: E402

STAGES = ("fetch", "decode", "synth", "augment", "extract", "shard")
CLIPS_PER_SHARD = 256
EVAL_SHARES = ("eval-speech", "eval-music", "eval-noise", "eval-tts")
LATENCY_CEILING_HOPS = 20  # the §7 ceiling M5's hit window extends past the endpoint
SPEED_DENOMINATOR = 1000   # the rational a speed draw is applied as (kws_audio.change_speed's rule)
REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
RATE = kws_audio.RATE
STATE_FILE = "clips.json"
# The card reads these toolchain keys (kws_dataset_card.GPL_SUBPROCESS_TOOLS / _TOOLCHAIN_KEYS).
TOOL_PIPER, TOOL_ESPEAK, TOOL_ONNXRUNTIME = "piper-tts", "espeak-ng", "onnxruntime"

Task = dict[str, Any]
Worker = Callable[[dict[str, str], Task], dict[str, Any]]


class BuildError(RuntimeError):
    """A condition the builder refuses to proceed past; the message names the clip or source and the rule."""


# ---------------------------------------------------------------- the build context


@dataclasses.dataclass
class Build:
    manifest: Manifest
    store: Store
    jobs: int = 1

    def __post_init__(self) -> None:
        self.hash = manifest_hash(self.manifest)
        self.recipe = self.manifest.recipe
        self.resampler = kws_audio.resampler_id(self.recipe.resampler.window)

    @property
    def features_dir(self) -> pathlib.Path:
        return self.store.features(self.hash)

    @property
    def state_path(self) -> pathlib.Path:
        return self.features_dir / STATE_FILE

    def pcm_dir(self, source_id: str) -> pathlib.Path:
        return self.store.pcm(source_id, kws_audio.DECODER_ID, self.resampler)

    def pcm_path(self, source_id: str, relative: str) -> pathlib.Path:
        return self.pcm_dir(source_id) / f"{relative}.wav"

    def augmented_path(self, clip_id: str, variant: int) -> pathlib.Path:
        return self.features_dir / "augmented" / f"{clip_id}.v{variant}.wav"

    def extracted_path(self, clip_id: str, variant: int) -> pathlib.Path:
        digest = hashlib.sha256(f"{clip_id}#{variant}".encode()).hexdigest()[:24]
        return self.features_dir / "extracted" / f"{digest}.npy"

    # -- the intermediate state ----------------------------------------------------------------
    def load_state(self, needs: str) -> dict[str, Any]:
        if not self.state_path.exists():
            raise BuildError(f"{self.state_path} missing — run `{needs}` first")
        state = json.loads(self.state_path.read_text(encoding="utf-8"))
        if state.get("manifest_hash") != self.hash:
            raise BuildError(f"{self.state_path} was written for manifest {state.get('manifest_hash')}, "
                             f"this manifest hashes to {self.hash}")
        if needs not in state.get("stages", []):
            raise BuildError(f"stage `{needs}` has not run for manifest {self.hash} — run it first")
        return state

    def save_state(self, state: dict[str, Any], stage: str) -> None:
        stages = [s for s in state.get("stages", []) if STAGES.index(s) < STAGES.index(stage)]
        state["stages"] = stages + [stage]
        state["manifest_hash"] = self.hash
        state["clips"].sort(key=lambda c: (c["id"], c["variant"]))
        self.features_dir.mkdir(parents=True, exist_ok=True)
        tmp = self.state_path.with_suffix(".tmp")
        text = json.dumps(state, indent=1, sort_keys=True, ensure_ascii=False) + "\n"
        tmp.write_text(text, encoding="utf-8")
        os.replace(tmp, self.state_path)

    def run(self, fn: Worker, tasks: list[Task], label: str) -> list[dict[str, Any]]:
        """Map `fn` over tasks (already in clip-id order); results come back in the same order."""
        ctx = {"manifest": str(self.manifest.path), "store": str(self.store.root)}
        if self.jobs <= 1 or len(tasks) < 2:
            return [fn(ctx, t) for t in tasks]
        if self.manifest.path is None:
            raise BuildError("--jobs > 1 needs a manifest loaded from a file (workers re-read it by path)")
        chunk = max(1, min(64, len(tasks) // (self.jobs * 4) + 1))
        with concurrent.futures.ProcessPoolExecutor(max_workers=self.jobs,
                                                    mp_context=multiprocessing.get_context("spawn")) as pool:
            return list(pool.map(fn, [ctx] * len(tasks), tasks, chunksize=chunk))


# ---------------------------------------------------------------- worker-side caches


_WORKER_BUILD: dict[str, Build] = {}
_WORKER_FRONTEND: dict[tuple, kws_features.FrontEnd] = {}


def _worker_build(ctx: dict[str, str]) -> Build:
    key = ctx["manifest"] + "\0" + ctx["store"]
    if key not in _WORKER_BUILD:
        _WORKER_BUILD[key] = Build(load_manifest(ctx["manifest"]), Store(pathlib.Path(ctx["store"])))
    return _WORKER_BUILD[key]


def _worker_frontend(g: kws_features.Geometry) -> kws_features.FrontEnd:
    key = tuple(g.as_array().tolist())
    if key not in _WORKER_FRONTEND:
        _WORKER_FRONTEND[key] = kws_features.FrontEnd(g)
    return _WORKER_FRONTEND[key]


def require_contract(build: Build) -> int:
    got = kws_features.contract_version()
    want = build.recipe.log_mel_contract_version
    if got != want:
        raise BuildError(f"recipe.log_mel_contract_version {want} != dsptap_log_mel_contract_version() "
                         f"{got}: the manifest was written for another front-end contract; rebuild the "
                         "manifest, not the features")
    return got


def require_verified_sources(build: Build) -> None:
    """Every stage after `fetch` refuses an archive whose verification no longer stands (marker + size)."""
    for source in sorted(build.manifest.sources, key=lambda s: s.id):
        build.store.require_verified(source)


# ---------------------------------------------------------------- fetch


def stage_fetch(build: Build) -> None:
    total = 0
    for source in sorted(build.manifest.sources, key=lambda s: s.id):
        path = build.store.fetch(source, REPO_ROOT)
        total += path.stat().st_size
        print(f"fetch: {source.id}: verified {path.name} ({path.stat().st_size} bytes)")
    print(f"fetch: {len(build.manifest.sources)} archives, {total} bytes")


# ---------------------------------------------------------------- decode


def label_window(recipe: Recipe) -> int:
    """The trim rule's window in samples (kws_audio.endpoint's own rounding)."""
    return max(1, int(round(recipe.label.window_ms * 1e-3 * RATE)))


def _decode_one(ctx: dict[str, str], task: Task) -> dict[str, Any]:
    build = _worker_build(ctx)
    recipe = build.recipe
    src = pathlib.Path(task["path"])
    try:
        x, fs = kws_audio.read_audio(src)
    except (soundfile.SoundFileError, RuntimeError) as e:
        raise BuildError(f"clip {task['id']}: cannot decode {task['relative']} ({e}) — the archive verified "
                         "but this member is not decodable audio") from None
    if task["pcm_exact"] and fs != RATE:
        raise BuildError(f"clip {task['id']}: {fs} Hz but its source is declared pcm_exact (16 kHz PCM) — "
                         "the decoded-PCM sha256 would not be an identity field; unset pcm_exact")
    y = kws_audio.resample(x, fs, RATE, recipe.resampler.window)
    x16 = kws_audio.to_int16(y)
    if x16.size == 0:
        raise BuildError(f"clip {task['id']}: decodes to zero samples")
    if not np.any(x16):
        raise BuildError(f"clip {task['id']}: decodes to digital silence ({x16.size} zero samples) — it "
                         "cannot be labelled or mixed at an SNR")
    out = build.pcm_path(task["source"], task["relative"])
    if not out.exists():
        kws_audio.write_pcm(out, x16)
    result: dict[str, Any] = {"id": task["id"], "length": int(x16.size), "endpoint": None, "fs_in": fs,
                              "pcm_sha256": kws_audio.pcm_sha256(x16)}
    xf = kws_audio.from_int16(x16)
    if task["want_endpoint"]:
        win = label_window(recipe)
        if x16.size < win:
            raise BuildError(f"clip {task['id']}: {x16.size} samples is shorter than one "
                             f"{recipe.label.window_ms:g} ms label window ({win} samples) — no endpoint "
                             "can be measured")
        e = kws_audio.endpoint(xf, recipe.label.trim_db, recipe.label.window_ms)
        if e is None:
            raise BuildError(f"clip {task['id']}: positive but silent — no keyword endpoint under the trim "
                             "rule")
        result["endpoint"] = int(e)
    if task["material"] == "rir":
        result["onset"] = int(kws_audio.rir_onset(xf))
        result["rt60_s"] = kws_audio.rt60_t20(xf)  # extra.rt60_s: the augment policy's rt60_s selects on it
    return result


def share_for(source: Source, split: str, material: str) -> str | None:
    """The share a clip of `material` lands in for `split`; None when the source's roles do not cover it.

    Roles: `train` / `dev` featurize the clip in that split; `aug` supplies mix-in material (noise, RIRs, or
    speech/music with no `train`/`dev` role) to whichever training-side split its key hashes into — never
    featurized; the eval roles name the eval share by material.
    """
    roles = source.roles
    if split == "eval":
        share = {"speech": "eval-speech", "music": "eval-music", "noise": "eval-noise",
                 "tts": "eval-tts"}.get(material)
        return share if share in roles else None
    if material in ("noise", "rir"):
        return "aug" if "aug" in roles else None
    if split in roles:
        return split
    return "aug" if "aug" in roles else None


def rir_split(recipe: Recipe, key: str) -> str:
    """RIRs split train / dev only (the hold-out's rooms are real): the split fractions renormalized
    over the two, keyed like everything else. The toy fixture is cut by this rule too."""
    f = recipe.split.fractions
    total = f["train"] + f["dev"]
    if total <= 0.0:
        raise BuildError("RIRs split train/dev only, but both fractions are 0")
    return kws_audio.assign_split(recipe.split.salt, key,
                                  {"train": f["train"] / total, "dev": f["dev"] / total, "eval": 0.0})


def assign(build: Build, source: Source, item: kws_sources.Listed) -> tuple[str, str | None]:
    recipe = build.recipe
    if item.official_split is not None:
        split = item.official_split
    elif source.material == "rir":
        split = rir_split(recipe, item.clip.key)
    else:
        split = kws_audio.assign_split(recipe.split.salt, item.clip.key, recipe.split.fractions)
    return split, share_for(source, split, source.material)


def stage_decode(build: Build) -> None:
    require_contract(build)
    recipe = build.recipe
    state: dict[str, Any] = {"clips": [], "keywords": {}, "notes": [], "excluded_by_role": {}}
    listed: dict[str, tuple[Source, kws_sources.Listed]] = {}
    tasks: list[Task] = []
    for source in sorted(build.manifest.sources, key=lambda s: s.id):
        root = build.store.extract(source)
        listing = kws_sources.list_source(source, root, recipe)
        state["notes"].extend(listing.notes)
        if listing.keywords:
            words = "\n".join(listing.keywords)
            state["keywords"][source.id] = {"count": len(listing.keywords),
                                            "sha256": hashlib.sha256(words.encode()).hexdigest(),
                                            "words": listing.keywords}
        if not listing.clips:
            continue
        pcm_dir = build.pcm_dir(source.id)
        marker = pcm_dir / ".archive-sha256"
        if pcm_dir.exists() and (not marker.exists()
                                 or marker.read_text(encoding="utf-8").strip() != source.archive.sha256):
            shutil.rmtree(pcm_dir)
        pcm_dir.mkdir(parents=True, exist_ok=True)
        marker.write_text(source.archive.sha256 + "\n", encoding="utf-8")
        for item in listing.clips:
            split, share = assign(build, source, item)
            if share is None:
                key = f"{source.id}:{split}"
                state["excluded_by_role"][key] = state["excluded_by_role"].get(key, 0) + 1
                continue
            clip = item.clip
            clip.split, clip.share = split, share
            if clip.material in ("noise", "rir") or share == "aug":
                clip.label = None
            listed[clip.id] = (source, item)
            tasks.append({"id": clip.id, "source": source.id, "relative": item.relative,
                          "path": str(item.path), "pcm_exact": source.pcm_exact,
                          "want_endpoint": clip.label == 1, "material": clip.material})
    if not tasks and not state["keywords"]:
        raise BuildError("decode: the manifest yields no clips after the role gate")
    tasks.sort(key=lambda t: t["id"])
    t0 = time.time()
    results = build.run(_decode_one, tasks, "decode")
    for task, res in zip(tasks, results):
        source, item = listed[task["id"]]
        clip = item.clip
        clip.length = res["length"]
        clip.pcm_sha256 = res["pcm_sha256"]
        clip.endpoint_sample = res["endpoint"]
        if res["endpoint"] is not None:
            clip.extra["endpoint_dry"] = res["endpoint"]
        if res["fs_in"] != RATE:
            clip.extra["source_rate"] = res["fs_in"]
        if clip.material == "rir":
            clip.extra["onset"] = res["onset"]
            clip.extra["rt60_s"] = res["rt60_s"]
        state["clips"].append(dataclasses.asdict(clip))
    for key, n in sorted(state["excluded_by_role"].items()):
        state["notes"].append(f"{key}: {n} clips excluded — the source's roles do not cover that split")
    build.save_state(state, "decode")
    print(f"decode: {len(tasks)} clips from {len(build.manifest.sources)} sources in "
          f"{time.time() - t0:.1f} s (--jobs {build.jobs}); notes: {len(state['notes'])}")
    for note in state["notes"]:
        print(f"decode: note: {note}")


# ---------------------------------------------------------------- synth (M4b; the invocation exists at M4a)


def piper_command(piper: str, voice: dict[str, Any], out: pathlib.Path, speaker: int | None,
                  length_scale: float, noise_scale: float, noise_w: float) -> list[str]:
    cmd = [piper, "--model", voice["onnx"], "--config", voice["config"], "--output_file", str(out),
           "--length_scale", f"{length_scale:.4f}", "--noise_scale", f"{noise_scale:.4f}",
           "--noise_w", f"{noise_w:.4f}"]
    if speaker is not None:
        cmd += ["--speaker", str(speaker)]
    return cmd


def piper_versions(piper: str) -> dict[str, str]:
    """The piper-tts, ONNX Runtime and espeak-ng versions `piper` runs, for the lock's toolchain.

    piper's CLI has no --version (checked against piper-tts 1.8.0, 9 September 2026); the versions come
    from the package metadata of the interpreter its launcher names (`#!<python>` on its first line), which
    is where `piper` actually runs. piper >= 1.3 embeds espeak-ng as a compiled bridge that reports no
    version of its own, so that entry names the piper-tts release it is embedded in rather than inventing a
    number. Anything that cannot be read is recorded as such, never guessed.
    """
    not_obtainable = "not obtainable"
    versions = {TOOL_PIPER: not_obtainable, TOOL_ONNXRUNTIME: not_obtainable, TOOL_ESPEAK: not_obtainable}
    interpreter: str | None = None
    try:
        with open(piper, "rb") as f:
            first = f.readline(4096)
        if first.startswith(b"#!"):
            candidate = first[2:].decode("utf-8", "replace").strip().split()
            if candidate and "python" in pathlib.Path(candidate[-1]).name:
                interpreter = candidate[-1]
    except OSError:
        interpreter = None
    if interpreter is None:
        return versions
    probe = ("import importlib.metadata as m, json\n"
             "out = {}\n"
             "for name in ('piper-tts', 'onnxruntime'):\n"
             "    try:\n"
             "        out[name] = m.version(name)\n"
             "    except m.PackageNotFoundError:\n"
             "        pass\n"
             "print(json.dumps(out))\n")
    try:
        proc = subprocess.run([interpreter, "-c", probe], capture_output=True, text=True, check=False,
                              timeout=60)
        found = json.loads(proc.stdout.strip() or "{}") if proc.returncode == 0 else {}
    except (OSError, ValueError, subprocess.TimeoutExpired):
        found = {}
    if "piper-tts" in found:
        versions[TOOL_PIPER] = found["piper-tts"]
        versions[TOOL_ESPEAK] = (f"embedded in piper-tts {found['piper-tts']} (piper.espeakbridge; the "
                                 "bridge reports no separate espeak-ng version)")
    if "onnxruntime" in found:
        versions[TOOL_ONNXRUNTIME] = found["onnxruntime"]
    return versions


def _voice_member(source: Source, root: pathlib.Path, voice_id: str, field: str, member: str) -> pathlib.Path:
    if not safe_member(member) or not (root / member).resolve().is_relative_to(root.resolve()):
        raise BuildError(f"synth: voice {voice_id!r}: {field} {member!r} must name a member of source "
                         f"{source.id!r}'s archive (relative, no '..')")
    p = root / member
    if not p.is_file():
        raise BuildError(f"synth: voice {voice_id!r}: {field} {member!r} is not in source {source.id!r}'s "
                         f"archive ({root}); voices are pinned .onnx + .json placed by hand, never "
                         "downloaded")
    return p


def stage_synth(build: Build) -> None:
    require_contract(build)
    require_verified_sources(build)
    tts = build.recipe.tts
    if not tts:
        raise BuildError("synth: recipe.tts is absent — nothing to synthesize (the TTS block is M4b's; "
                         "the toy manifest carries no voice)")
    piper = shutil.which("piper")
    if piper is None:
        raise BuildError("synth: `piper` is not on PATH — install piper-tts >= 1.3 (a build-time subprocess "
                         "tool, never imported, never redistributed); the builder never downloads voices")
    state = build.load_state("decode")
    recipe = build.recipe
    seed = recipe.augment.seed
    texts = tts["texts"]
    tts_dir = build.features_dir / "tts"
    n_per_speaker = int(tts.get("positives_per_speaker", 1))
    tools = piper_versions(piper)
    rows: list[dict[str, Any]] = []
    excluded: dict[str, int] = {}
    t0 = time.time()
    for voice in sorted(tts["voices"], key=lambda v: v["id"]):
        source = build.manifest.source(voice["source"])
        root = build.store.extract(source)
        onnx = _voice_member(source, root, voice["id"], "onnx", voice["onnx"])
        config = _voice_member(source, root, voice["id"], "config", voice["config"])
        digest = sha256_file(onnx)
        if digest != voice["sha256"]:
            raise BuildError(f"synth: voice {voice['id']!r}: sha256 {digest} != manifest {voice['sha256']}")
        for speaker in voice.get("speakers") or [None]:
            spk = "0" if speaker is None else str(speaker)  # R3's convention: "0" for a single-speaker voice
            key = f"{voice['id']}:{spk}"
            split = kws_audio.assign_split(recipe.split.salt, key, recipe.split.fractions)
            share = share_for(source, split, "tts")
            for label, kind in ((1, "positive"), (0, "negative")):
                for k, text in enumerate(texts.get(kind, [])):
                    for n in range(n_per_speaker):
                        relative = f"{voice['id']}/{spk}/{kind}-{k:03d}-{n:03d}"
                        clip_id = kws_sources.clip_id(source, relative)
                        if share is None:
                            excluded[f"{source.id}:{split}"] = excluded.get(f"{source.id}:{split}", 0) + 1
                            continue
                        # its own generator key, so the variant-0 key stays the eval positive's context draw
                        rng = kws_audio.draw_rng(seed, clip_id + "#tts", 0)
                        ls = float(rng.uniform(*tts.get("length_scale", (0.9, 1.1))))
                        ns = float(rng.uniform(*tts.get("noise_scale", (0.5, 0.8))))
                        nw = float(rng.uniform(*tts.get("noise_w", (0.6, 1.0))))
                        out = tts_dir / f"{relative}.wav"
                        out.parent.mkdir(parents=True, exist_ok=True)
                        cmd = piper_command(piper, {"onnx": str(onnx), "config": str(config)}, out, speaker,
                                            ls, ns, nw)
                        proc = subprocess.run(cmd, input=text, text=True, capture_output=True, check=False)
                        if proc.returncode != 0:
                            raise BuildError(f"synth: piper failed on {clip_id}: {proc.stderr.strip()}")
                        try:
                            x, fs = kws_audio.read_audio(out)
                        except (soundfile.SoundFileError, RuntimeError) as e:
                            raise BuildError(f"synth: piper wrote no decodable audio for {clip_id}: "
                                             f"{e}") from None
                        y = kws_audio.resample(x, fs, RATE, recipe.resampler.window)
                        x16 = kws_audio.to_int16(y)
                        if x16.size == 0 or not np.any(x16):
                            raise BuildError(f"synth: {clip_id}: piper produced {x16.size} samples of "
                                             "silence")
                        pcm = build.pcm_path(source.id, relative)
                        kws_audio.write_pcm(pcm, x16)
                        e: int | None = None
                        if label == 1:
                            e = kws_audio.endpoint(kws_audio.from_int16(x16), recipe.label.trim_db,
                                                   recipe.label.window_ms)
                            if e is None:
                                raise BuildError(f"synth: {clip_id}: positive but silent — no keyword "
                                                 "endpoint under the trim rule")
                        extra = {"voice": voice["id"], "voice_sha256": digest, "length_scale": ls,
                                 "noise_scale": ns, "noise_w": nw, "text_variant": k, "text": text,
                                 "source_rate": fs, "endpoint_dry": e}
                        rows.append(dataclasses.asdict(Clip(
                            id=clip_id, source=source.id, material="tts", split=split, share=share,
                            label=label, endpoint_sample=e, length=int(x16.size), key=key,
                            pcm_sha256=kws_audio.pcm_sha256(x16), speaker=spk, extra=extra)))
    for key, n in sorted(excluded.items()):
        state["excluded_by_role"][key] = state["excluded_by_role"].get(key, 0) + n
        state["notes"].append(f"{key}: {n} TTS clips excluded — the source's roles do not cover that split")
    state["clips"] = [c for c in state["clips"] if c["material"] != "tts"] + rows
    state["tools"] = tools
    build.save_state(state, "synth")
    print(f"synth: {len(rows)} clips in {time.time() - t0:.1f} s (piper-tts {tools[TOOL_PIPER]}, "
          f"onnxruntime {tools[TOOL_ONNXRUNTIME]})")


# ---------------------------------------------------------------- augment


def applied_speed(drawn: float) -> float:
    """The factor change_speed applies for a drawn speed: Fraction(drawn).limit_denominator(1000) as a float.

    Recorded in the lock's draw as `speed`; float(p / q) with q <= 1000 round-trips through the same rule
    (checked over 200,000 draws in [0.9, 1.1] at 6 decimals, 9 September 2026), so `_render` applies exactly
    the recorded factor and asserts it.
    """
    frac = Fraction(drawn).limit_denominator(SPEED_DENOMINATOR)
    return frac.numerator / frac.denominator


def rendered_length(length: int, speed: float | None, rir_length: int | None) -> int:
    """The sample count a render produces: resample_poly gives ceil(n * up / down), convolution
    n + len(h) - 1 (both verified against scipy 1.18.1 on 9 September 2026)."""
    n = int(length)
    if speed is not None:
        frac = Fraction(speed).limit_denominator(SPEED_DENOMINATOR)
        n = math.ceil(n * frac.denominator / frac.numerator)
    if rir_length is not None:
        n = n + int(rir_length) - 1
    return n


@dataclasses.dataclass
class Pools:
    """Same-split material, sorted by clip id, that a draw indexes into."""

    noise: dict[str, list[dict[str, Any]]]
    rir: dict[str, list[dict[str, Any]]]
    context: dict[str, list[dict[str, Any]]]
    rir_rt60: dict[str, list[float | None]]  # every same-split RIR's measured RT60, for the refusal message


def build_pools(build: Build, clips: list[dict[str, Any]]) -> Pools:
    lo, hi = build.recipe.augment.rt60_s
    pools = Pools({}, {}, {}, {})
    for c in clips:
        if c["variant"] != 0:
            continue
        s = c["split"]
        if c["material"] == "noise":
            pools.noise.setdefault(s, []).append(c)
        elif c["material"] == "rir":
            rt = c["extra"].get("rt60_s")
            pools.rir_rt60.setdefault(s, []).append(rt)
            if rt is not None and lo <= rt <= hi:
                pools.rir.setdefault(s, []).append(c)
        aug_material = c["share"] == "aug" and c["material"] in ("speech", "music")
        if c["material"] == "noise" or c["label"] == 0 or aug_material:
            pools.context.setdefault(s, []).append(c)
    for d in (pools.noise, pools.rir, pools.context):
        for v in d.values():
            v.sort(key=lambda c: c["id"])
    return pools


def resolve_draw(build: Build, clip: dict[str, Any], variant: int, pools: Pools,
                 augmented: bool) -> dict[str, Any]:
    """The resolved draw for (clip, variant): in the fixed order dry?, gain, speed, RIR, noise, SNR, context.

    Every value is drawn even for a dry variant so the generator's sequence is fixed; the noise crop start
    inside `mix_snr` comes from its own generator, draw_rng(seed, clip_id + "#noise", variant). `speed` is
    recorded as the rational factor actually applied (`applied_speed`).
    """
    recipe = build.recipe
    pol = recipe.augment
    rng = kws_audio.draw_rng(pol.seed, clip["id"], variant)
    split = clip["split"]
    draw: dict[str, Any] = {"gain_db": 0.0, "snr_db": None, "noise": None, "speed": 1.0, "rir": None,
                            "rir_delay": 0, "context": [], "offset": 0}
    speed: float | None = None
    rir_length: int | None = None
    if augmented:
        dry = bool(rng.uniform() < pol.dry_share)
        draw["gain_db"] = round(float(rng.uniform(*pol.gain_db)), 6)
        speed_draw = applied_speed(round(float(rng.uniform(*pol.speed)), 6))
        rir_u = float(rng.uniform())
        noise_u = float(rng.uniform())
        snr_draw = round(float(rng.uniform(*pol.snr_db)), 6)
        if not dry:
            draw["speed"] = speed_draw
            speed = speed_draw
            rirs = pools.rir.get(split, [])
            if not rirs:
                have = sorted(pools.rir_rt60.get(split, []), key=lambda v: (v is None, v))
                raise BuildError(f"clip {clip['id']} variant {variant}: no {split}-split RIR inside rt60_s "
                                 f"{list(pol.rt60_s)} to draw from (RIR material must be in the same split; "
                                 f"measured RT60s available: {have})")
            rir = rirs[int(rir_u * len(rirs))]
            draw["rir"] = rir["id"]
            draw["rir_delay"] = int(rir["extra"]["onset"])
            rir_length = int(rir["length"])
            noises = pools.noise.get(split, [])
            if not noises:
                raise BuildError(f"clip {clip['id']} variant {variant}: no {split}-split noise material to "
                                 "mix at SNR — the noise share must exist in the same split (the plan's "
                                 "rule)")
            draw["noise"] = noises[int(noise_u * len(noises))]["id"]
            draw["snr_db"] = snr_draw
    if clip["label"] == 1:
        n_ctx = int(round(recipe.context_s * RATE))
        g = recipe.geometry
        n_trail = (LATENCY_CEILING_HOPS + recipe.label.tolerance_hops) * g.hop + g.frame
        # offset in [n_ctx, n_ctx + n_ctx // 2], both ends inclusive (numpy's integers excludes `high`)
        offset = n_ctx + int(rng.integers(0, n_ctx // 2 + 1)) if n_ctx > 0 else 0
        need = offset + rendered_length(clip["length"], speed, rir_length) + n_trail
        pool = pools.context.get(split, [])
        if need > 0 and not pool:
            raise BuildError(f"clip {clip['id']} variant {variant}: no {split}-split negative material to "
                             "embed the positive in (context_s > 0 needs same-split negatives or noise)")
        have = 0
        context: list[str] = []
        while have < need:
            c = pool[int(rng.integers(0, len(pool)))]
            context.append(c["id"])
            have += int(c["length"])
        draw["context"] = context
        draw["offset"] = offset
    return draw


def _relative(clip: dict[str, Any]) -> str:
    return clip["id"][len(clip["source"]) + 1:]


def _read_clip(build: Build, clip: dict[str, Any]) -> np.ndarray:
    return kws_audio.from_int16(kws_audio.read_pcm(build.pcm_path(clip["source"], _relative(clip))))


def _speed(build: Build, y: np.ndarray, draw: dict[str, Any], clip_id: str, variant: int) -> np.ndarray:
    """Gain, then the speed change, asserting the factor change_speed applied is the one the lock records."""
    y = kws_audio.gain_db(y, draw["gain_db"])
    if draw["speed"] != 1.0:
        y, applied = kws_audio.change_speed(y, draw["speed"], build.recipe.resampler.window)
        if applied != draw["speed"]:
            raise BuildError(f"clip {clip_id} variant {variant}: change_speed applied {applied!r}, the lock "
                             f"records speed {draw['speed']!r}")
    return y


def _treat(build: Build, x: np.ndarray, draw: dict[str, Any], clip_id: str, variant: int,
           by_id: dict[str, dict[str, Any]], ref: slice | None = None) -> np.ndarray:
    """The RIR and the SNR noise of a draw over the whole of x (a negative clip, or a positive's mixture);
    `ref` is the span whose RMS the SNR is measured against (the keyword's extent inside a mixture)."""
    if draw["rir"] is not None:
        r = by_id[draw["rir"]]
        h = _read_clip(build, r)
        x, onset = kws_audio.convolve_rir(x, h)
        if onset != draw["rir_delay"]:
            raise BuildError(f"clip {clip_id} variant {variant}: RIR {draw['rir']} onset {onset} != the "
                             f"lock's rir_delay {draw['rir_delay']}")
    if draw["noise"] is not None:
        noise = _read_clip(build, by_id[draw["noise"]])
        crop_rng = kws_audio.draw_rng(build.recipe.augment.seed, clip_id + "#noise", variant)
        try:
            x = kws_audio.mix_snr(x, noise, draw["snr_db"], crop_rng, ref=ref)
        except ValueError as e:
            raise BuildError(f"clip {clip_id} variant {variant}: noise {draw['noise']} cannot be mixed at "
                             f"{draw['snr_db']} dB — {e}") from None
    return x


def _augment_one(ctx: dict[str, str], task: Task) -> dict[str, Any]:
    build = _worker_build(ctx)
    clip = task["clip"]
    draw = task["draw"]
    y = _speed(build, _read_clip(build, clip), draw, clip["id"], task["variant"])
    # a negative is treated whole here; a positive's RIR and noise cover its whole mixture (extract)
    if clip["label"] != 1:
        y = _treat(build, y, draw, clip["id"], task["variant"], task["by_id"])
    y16 = kws_audio.to_int16(kws_audio.peak_normalize(y))
    if y16.size != task["planned_length"]:
        raise BuildError(f"clip {clip['id']} variant {task['variant']}: rendered {y16.size} samples, the "
                         f"resolved draw planned {task['planned_length']}")
    kws_audio.write_pcm(build.augmented_path(clip["id"], task["variant"]), y16)
    return {"id": clip["id"], "variant": task["variant"], "length": int(y16.size),
            "pcm_sha256": kws_audio.pcm_sha256(y16)}


def stage_augment(build: Build) -> None:
    require_contract(build)
    require_verified_sources(build)
    state = build.load_state("decode")
    pol = build.recipe.augment
    dry = [c for c in state["clips"] if c["variant"] == 0]
    by_id = {c["id"]: c for c in dry}
    pools = build_pools(build, dry)
    aug_dir = build.features_dir / "augmented"
    if aug_dir.exists():
        shutil.rmtree(aug_dir)
    rows: list[dict[str, Any]] = []
    tasks: list[Task] = []
    t0 = time.time()
    for c in sorted(dry, key=lambda c: c["id"]):
        c["draw"] = None
        if c["label"] is None:
            continue
        if c["split"] == "eval":
            if c["label"] == 1:
                c["draw"] = resolve_draw(build, c, 0, pools, augmented=False)
                c["endpoint_sample"] = kws_audio.transform_endpoint(c["extra"]["endpoint_dry"],
                                                                    offset=c["draw"]["offset"])
            continue
        n_variants = pol.draws_per_positive if c["label"] == 1 else pol.noisy_per_negative
        variants = range(1, n_variants + 1)
        for v in variants:
            draw = resolve_draw(build, c, v, pools, augmented=True)
            speed = draw["speed"] if draw["speed"] != 1.0 else None
            rir_len = int(by_id[draw["rir"]]["length"]) if draw["rir"] else None
            # a positive's augmented-tier file is the gain+speed keyword (its RIR/noise cover the mixture)
            planned = rendered_length(c["length"], speed, None if c["label"] == 1 else rir_len)
            row = dict(c)
            row["extra"] = dict(c["extra"])
            row["variant"] = v
            row["draw"] = draw
            row["length"] = planned
            if c["label"] == 1:
                row["endpoint_sample"] = kws_audio.transform_endpoint(
                    c["extra"]["endpoint_dry"], draw["speed"], draw["rir_delay"], draw["offset"])
            rows.append(row)
            needed = {i: by_id[i] for i in (draw["rir"], draw["noise"]) if i}
            tasks.append({"clip": c, "variant": v, "draw": draw, "planned_length": planned, "by_id": needed})
    results = build.run(_augment_one, tasks, "augment")
    rendered = {(r["id"], r["variant"]): r for r in results}
    for row in rows:
        r = rendered[(row["id"], row["variant"])]
        row["pcm_sha256"] = r["pcm_sha256"]
    state["clips"] = dry + rows
    build.save_state(state, "augment")
    print(f"augment: {len(rows)} variants of {len(dry)} clips in {time.time() - t0:.1f} s "
          f"(--jobs {build.jobs})")


# ---------------------------------------------------------------- extract


def featurized(clips: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """The rows that carry features: labelled eval clips, train/dev negatives (clean + draws), positive
    draws."""
    out = []
    for c in clips:
        if c["label"] is None:
            continue
        if c["split"] == "eval" or c["variant"] > 0 or c["label"] == 0:
            out.append(c)
    return sorted(out, key=lambda c: (c["id"], c["variant"]))


def _audio_path(build: Build, clip: dict[str, Any]) -> pathlib.Path:
    if clip["variant"] > 0:
        return build.augmented_path(clip["id"], clip["variant"])
    return build.pcm_path(clip["source"], _relative(clip))


def draw_refs(draw: dict[str, Any] | None) -> list[str]:
    """Every clip id a draw names: its context clips, its RIR and its noise clip."""
    if not draw:
        return []
    return list(draw.get("context") or []) + [i for i in (draw.get("rir"), draw.get("noise")) if i]


def mixture(build: Build, y: np.ndarray, clip: dict[str, Any], by_id: dict[str, dict[str, Any]],
            n_trail: int) -> np.ndarray:
    """The signal a row is featurized from: the clip alone, or the positive embedded in its drawn context
    stream with the draw's RIR and SNR noise applied over the whole mixture.

    Before the RIR the mixture is stream[:offset] + keyword + stream[offset + len : total - tail], where
    tail = len(RIR) - 1; the convolution restores the full `total` = offset + len + tail + n_trail samples
    (the keyword's reverberant extent plus the trailing hops). The SNR is measured against the keyword's
    extent [offset, offset + len + tail). A treated mixture is peak-normalised to 0.99 like every rendered
    variant; an untreated one (an eval positive) is the raw concatenation.
    """
    draw = clip.get("draw")
    if not draw or not draw["context"]:
        return y
    offset = int(draw["offset"])
    tail = int(by_id[draw["rir"]]["length"]) - 1 if draw["rir"] else 0
    total = offset + y.size + tail + n_trail
    stream = np.concatenate([_read_clip(build, by_id[i]) for i in draw["context"]])
    if stream.size < total:
        raise BuildError(f"context stream {draw['context']} is {stream.size} samples, the mixture needs "
                         f"{total}")
    x = np.concatenate([stream[:offset], y, stream[offset + y.size:total - tail]])
    if draw["rir"] is None and draw["noise"] is None:
        return x
    x = _treat(build, x, draw, clip["id"], clip["variant"], by_id, ref=slice(offset, offset + y.size + tail))
    if x.size != total:
        raise BuildError(f"clip {clip['id']} variant {clip['variant']}: mixture is {x.size} samples, planned "
                         f"{total}")
    return kws_audio.peak_normalize(x)


def _extract_one(ctx: dict[str, str], task: Task) -> dict[str, Any]:
    build = _worker_build(ctx)
    clip = task["clip"]
    g = build.recipe.geometry
    fe = _worker_frontend(g)
    y = kws_audio.from_int16(kws_audio.read_pcm(_audio_path(build, clip)))
    x = mixture(build, y, clip, task["by_id"], task["n_trail"])
    feats = fe.extract(x)  # one reset per mixture
    rows = int(feats.shape[0])
    hop_index = -1
    if clip["label"] == 1:
        e = int(clip["endpoint_sample"])
        hop_index = g.hop_index(e)
        if not 0 <= hop_index < rows:
            raise BuildError(f"clip {clip['id']} variant {clip['variant']}: stored endpoint {e} is hop "
                             f"{hop_index}, outside the {rows} rows of its mixture")
    out = build.extracted_path(clip["id"], clip["variant"])
    out.parent.mkdir(parents=True, exist_ok=True)
    np.save(out, feats.astype(np.float32), allow_pickle=False)
    return {"id": clip["id"], "variant": clip["variant"], "frames": rows, "endpoint_hop": hop_index,
            "samples": int(x.size)}


def stage_extract(build: Build) -> None:
    require_contract(build)
    require_verified_sources(build)
    state = build.load_state("augment")
    recipe = build.recipe
    g = recipe.geometry
    by_id = {c["id"]: c for c in state["clips"] if c["variant"] == 0}
    n_trail = (LATENCY_CEILING_HOPS + recipe.label.tolerance_hops) * g.hop + g.frame
    ext_dir = build.features_dir / "extracted"
    if ext_dir.exists():
        shutil.rmtree(ext_dir)
    rows = featurized(state["clips"])
    tasks = []
    for c in rows:
        needed = {i: by_id[i] for i in draw_refs(c.get("draw"))}
        tasks.append({"clip": c, "by_id": needed, "n_trail": n_trail})
    t0 = time.time()
    results = build.run(_extract_one, tasks, "extract")
    for c, r in zip(rows, results):
        c["extra"]["frames"] = r["frames"]
        c["extra"]["endpoint_hop"] = r["endpoint_hop"]
        c["extra"]["mixture_samples"] = r["samples"]
    # the front end that computed these rows: shard refuses to stamp another commit over them
    state["dsptap_commit"] = kws_features.dsptap_commit()
    build.save_state(state, "extract")
    frames = sum(r["frames"] for r in results)
    print(f"extract: {len(rows)} rows, {frames} frames in {time.time() - t0:.1f} s (--jobs {build.jobs})")


# ---------------------------------------------------------------- shard


def write_npz(path: pathlib.Path, arrays: dict[str, np.ndarray]) -> None:
    """np.load-compatible .npz with fixed member timestamps, so the file digest is a function of its arrays
    (np.savez stamps the zip members with the wall clock)."""
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for name, arr in arrays.items():
            buf = io.BytesIO()
            np.lib.format.write_array(buf, np.asarray(arr), allow_pickle=False)
            info = zipfile.ZipInfo(name + ".npy", date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            zf.writestr(info, buf.getvalue())


def shard_clip_id(c: dict[str, Any]) -> str:
    return c["id"] + (f"#{c['variant']}" if c["variant"] else "")


def hours(samples: int) -> float:
    return samples / RATE / 3600.0


def stored_path(g: kws_features.Geometry) -> str:
    """The path the geometry computes — what the shard header records (validate() requires recipe.stored_path
    to agree, so the label can never diverge from the features)."""
    return "pcen" if g.pcen.enabled else "log"


def toolchain(build: Build, state: dict[str, Any]) -> dict[str, Any]:
    """The lock's toolchain block; the synth stage's piper-tts / ONNX Runtime / espeak-ng versions join it
    from the ledger when synth ran."""
    out = {"python": platform.python_version(), "numpy": np.__version__, "scipy": scipy.__version__,
           "soundfile": soundfile.__version__, "libsndfile": soundfile.__libsndfile_version__,
           "decoder": kws_audio.DECODER_ID, "resampler": build.resampler,
           "dsptap_commit": kws_features.dsptap_commit(),
           "log_mel_contract_version": kws_features.contract_version(),
           "kws_features_version": kws_features.KWS_FEATURES_VERSION, "platform": platform.platform()}
    out.update(state.get("tools", {}))
    return out


def stage_shard(build: Build) -> None:
    contract = require_contract(build)
    require_verified_sources(build)
    state = build.load_state("extract")
    commit = kws_features.dsptap_commit()
    if state.get("dsptap_commit") != commit:
        raise BuildError(f"extract ran at DspTap {state.get('dsptap_commit')}, this checkout is {commit} — "
                         "rerun extract (the shard header names the front end that computed the features)")
    recipe = build.recipe
    g = recipe.geometry
    rows = featurized(state["clips"])
    header_base = {"log_mel_contract_version": contract,
                   "kws_features_version": kws_features.KWS_FEATURES_VERSION, "manifest_hash": build.hash,
                   "dsptap_commit": commit, "stored_path": stored_path(g)}
    shards: list[Shard] = []
    t0 = time.time()
    for split in ("train", "dev", "eval"):
        split_rows = [c for c in rows if c["split"] == split]
        out_dir = build.store.features(build.hash, split)
        if out_dir.exists():
            shutil.rmtree(out_dir)
        out_dir.mkdir(parents=True)
        for n, start in enumerate(range(0, len(split_rows), CLIPS_PER_SHARD)):
            chunk = split_rows[start:start + CLIPS_PER_SHARD]
            feats, offsets = [], [0]
            for c in chunk:
                f = np.load(build.extracted_path(c["id"], c["variant"]), allow_pickle=False)
                if f.shape != (c["extra"]["frames"], g.bands):
                    want = (c["extra"]["frames"], g.bands)
                    raise BuildError(f"clip {c['id']} variant {c['variant']}: extracted features {f.shape} "
                                     f"do not match the recorded {want} — rerun extract")
                feats.append(f)
                offsets.append(offsets[-1] + f.shape[0])
            arrays = {
                "features": np.concatenate(feats).astype(np.float32),
                "clip_ids": np.asarray([shard_clip_id(c) for c in chunk]),
                "clip_offsets": np.asarray(offsets, dtype=np.int64),
                "clip_labels": np.asarray([c["label"] for c in chunk], dtype=np.int8),
                "clip_endpoints": np.asarray([c["extra"]["endpoint_hop"] for c in chunk], dtype=np.int64),
                "clip_variants": np.asarray([c["variant"] for c in chunk], dtype=np.int32),
                "geometry": g.as_array(),
                "header": np.asarray(json.dumps({**header_base, "split": split}, sort_keys=True)),
            }
            path = out_dir / f"shard-{n:04d}.npz"
            write_npz(path, arrays)
            shards.append(Shard(split=split, file=f"{split}/{path.name}", sha256=sha256_file(path),
                                frames=int(offsets[-1]), clips=len(chunk)))
    # the lock
    clips = [Clip(**c) for c in state["clips"]]
    summary: dict[str, Any] = {"splits": {}, "featurized_rows": len(rows),
                               "decoded_clips": sum(1 for c in clips if c.variant == 0),
                               "clips_per_shard": CLIPS_PER_SHARD, "context_s": recipe.context_s,
                               "excluded_by_role": state.get("excluded_by_role", {}),
                               "keyword_lists": {k: {"count": v["count"], "sha256": v["sha256"]}
                                                 for k, v in state.get("keywords", {}).items()},
                               "notes": state.get("notes", [])}
    balance: dict[tuple, dict[str, int]] = {}
    for split in ("train", "dev", "eval"):
        per = {}
        for label, name in ((1, "positive"), (0, "negative")):
            sel = [c for c in rows if c["split"] == split and c["label"] == label]
            samples = sum(int(c["length"]) for c in sel)
            per[name] = {"count": len(sel), "hours": hours(samples),
                         "frames": sum(c["extra"]["frames"] for c in sel)}
        summary["splits"][split] = per
    # the class-balance table: the card's vocabulary (origin real / synthetic, render dry / augmented)
    for c in rows:
        key = (c["source"], "synthetic" if c["material"] == "tts" else "real",
               "augmented" if c["variant"] > 0 else "dry", c["extra"].get("keyword", ""),
               c["split"], c["share"], c["label"])
        b = balance.setdefault(key, {"count": 0, "samples": 0})
        b["count"] += 1
        b["samples"] += int(c["length"])
    class_balance = [{"source": k[0], "origin": k[1], "render": k[2], "keyword": k[3], "split": k[4],
                      "share": k[5], "label": k[6], "count": v["count"], "hours": hours(v["samples"])}
                     for k, v in sorted(balance.items())]
    ids = {share: eval_set_id(clips, share) for share in EVAL_SHARES if any(c.share == share for c in clips)}
    # The geometries the self-check ran at: the reference always; the manifest's too when it differs
    # (one record when they coincide — the card labels it "reference = manifest").
    try:
        self_check = {"reference": kws_features.self_check(kws_features.REFERENCE)}
        if g != kws_features.REFERENCE:
            self_check["manifest"] = kws_features.self_check(g)
    except AssertionError as e:
        raise BuildError(f"front-end self-check: {e}") from None
    lock = Lock(manifest_hash=build.hash, clips=clips, shards=shards, summary=summary,
                class_balance=class_balance, eval_set_id=ids, holdout_set_id=None,
                toolchain=toolchain(build, state), self_check=self_check)
    write_lock(lock, build.features_dir / "lock.json")
    ext_dir = build.features_dir / "extracted"
    if ext_dir.exists():
        shutil.rmtree(ext_dir)
    state["stages"] = [s for s in state["stages"] if s != "extract"]  # its per-clip files are gone
    build.save_state(state, "shard")
    print(f"shard: {len(shards)} shards, {len(rows)} rows, lock at {build.features_dir / 'lock.json'} "
          f"in {time.time() - t0:.1f} s")


# ---------------------------------------------------------------- main


def run_stage(build: Build, stage: str) -> None:
    if stage == "all":
        require_contract(build)
        for s in STAGES:
            if s == "synth" and not build.recipe.tts:
                print("synth: skipped - recipe.tts is absent (M4b)")
                continue
            run_stage(build, s)
        return
    {"fetch": stage_fetch, "decode": stage_decode, "synth": stage_synth, "augment": stage_augment,
     "extract": stage_extract, "shard": stage_shard}[stage](build)


REFUSALS = (BuildError, StoreError, ManifestError, kws_sources.SourceError, kws_features.BridgeError)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("stage", choices=STAGES + ("all",))
    ap.add_argument("--manifest", required=True, help="manifest.json")
    ap.add_argument("--store", help="the feature store directory (or $MUTAP_KWS_STORE); there is no default")
    ap.add_argument("--jobs", type=int, default=1,
                    help="process-pool size for decode/augment/extract (default 1)")
    args = ap.parse_args(argv)
    for stream in (sys.stdout, sys.stderr):  # refusal messages carry non-ASCII; never die printing one
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(errors="backslashreplace")
    try:
        # the inputs: a mistyped path or an unreadable manifest is a refusal, not a traceback
        try:
            manifest = load_manifest(args.manifest)
            store = Store(resolve_store(args.store))
        except ManifestError:
            raise
        except (OSError, ValueError) as e:
            raise ManifestError(f"{args.manifest}: {e}") from None
        build = Build(manifest, store, max(1, args.jobs))
        t0 = time.time()
        run_stage(build, args.stage)
        print(f"{args.stage}: done in {time.time() - t0:.1f} s (manifest {build.hash[:12]}, "
              f"store {store.root})")
    except REFUSALS as e:  # every other exception is a programming error and keeps its traceback
        print(f"refused: {e}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
