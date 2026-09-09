#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""kws_dataset_card — render DATASET.md and ATTRIBUTION.csv from a manifest and its lock.

Nothing in the card is typed by hand: every table is a function of `manifest.json`
(sources[] and recipe) and the build-emitted `lock.json`, so "every hour accounted
for with a licence" is a script output, and a rebuild with a different manifest
produces a different card. No wall-clock time enters the output; the same inputs
render byte-identical files.

    python3 tools/ml/kws/kws_dataset_card.py --manifest M --lock L [--out-dir tools/ml/kws]
    python3 tools/ml/kws/kws_dataset_card.py --manifest M --lock L --check   # refuse only, write nothing

The licence checks run before anything is written, and every failure names the
clip and the rule (house rule: refuse, never warn):

- every clip's effective licence (its `extra.licence` where the source carries
  per-file attribution, else its source's `licence`) is non-empty;
- no clip is flagged `extra.unlicensed`;
- no effective licence id carries a non-commercial `NC` term (CC BY-NC and
  friends), matched as a token so `CC0`, `Apache-2.0` and `INC` never trip it;
- every clip's source is in the manifest, and the lock is this manifest's build.

Per-clip fields the card reads from `Clip.extra` when an adapter sets them:
`licence` (per-file licence id), `attribution` (per-file attribution text —
MUSAN's per-subset LICENSE, FMA's per-track row), `artist`, `title`/`track`,
`keyword`, `near_miss`, `synthetic`, `unlicensed`. Absent keys fall back to the
source-level values.

When the lock's build did not run `synth`, the tooling section also cites the
committed hand-run record (`fixtures/synth_hand_run/lock_excerpt.json`, the
plan's M4a deliverable: synth exercised over one pinned voice, its TTS rows and
toolchain kept, no audio) and renders that record's tool versions from the file
— still nothing typed by hand.
"""
from __future__ import annotations

import argparse
import csv
import io
import pathlib
import re
import sys
from collections import OrderedDict
from typing import Any, Iterable

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from kws_features import REFERENCE  # noqa: E402
from kws_manifest import (  # noqa: E402
    Clip,
    Lock,
    Manifest,
    ManifestError,
    Source,
    load_manifest,
    manifest_hash,
    read_lock,
)

RATE = 16000  # the lock's `length` is samples at 16 kHz (kws_audio.RATE); hours = length / RATE / 3600

CARD_NAME = "DATASET.md"
ATTRIBUTION_NAME = "ATTRIBUTION.csv"
ATTRIBUTION_COLUMNS = ("source", "clip", "kind", "release", "material", "licence", "attribution",
                       "terms_accepted", "terms_verified", "redistributable", "origin", "roles")

# A non-commercial term inside a licence id, as a token: "CC-BY-NC-4.0", "CC BY-NC-SA 3.0", "cc-by-nc"
# — but not "CC0", "INC", "Apache-2.0".
_NC_TOKEN = re.compile(r"(?<![A-Za-z0-9])NC(?![A-Za-z0-9])", re.IGNORECASE)

# The split key each ingestion adapter assigns by (plan §6 M4 "Splits"; README "Splits").
SPLIT_KEY_RULE = {
    "speech_commands_v2": "the official hash split: validation_list.txt → dev, testing_list.txt → eval, "
                          "the rest train; key = the speaker hash before `_nohash_` (the same key the "
                          "official lists were drawn by)",
    "musan": "noise and speech partitions: file id; music: the artist/composer column of the subset's "
             "ANNOTATIONS where present, else file id — music is its own split axis, artist-disjoint",
    "openslr_28_simulated": "room id (simulated RIR files, train / dev; the hold-out's rooms are real)",
    "keyword_list": "no audio; not split",
    "mswc_keywords": "no audio; not split",
    "holdout": "never split: the hold-out is read once per release candidate",
    "common_voice": "client_id (M4b)",
    "ami": "participant set; a meeting whose participants span splits is excluded (M4b)",
    "fma": "(source, artist, track): music is its own split axis, artist-disjoint (M4b)",
    "piper": "voice + speaker id (`<voice>:<speaker>`, speaker \"0\" for a single-speaker voice), so a voice "
             "is placed whole; M4b keeps `kristin` and `john` together",
}

# Build-time tools that are never imported by distributed code and never redistributed. Versions come
# from the lock's toolchain block when the build ran them; a row without a version says so.
GPL_SUBPROCESS_TOOLS = (
    ("piper-tts", ("piper-tts", "piper"), "GPL-3.0-or-later",
     "text-to-speech synthesis of positives and TTS negatives (`synth`), run as a subprocess in the "
     "`--jobs` pool; never imported, never redistributed; its output is not a covered work"),
    ("espeak-ng", ("espeak-ng", "espeak"), "GPL-3.0-or-later",
     "Piper's phonemizer and the mining tool's grapheme-to-phoneme step, run as a subprocess; never "
     "imported, never redistributed"),
)
GPL_ROW_SUFFIX = " — GPL build-time subprocess tool, never redistributed"

_TOOLCHAIN_KEYS = {
    "python": ("python", "python_version"),
    "numpy": ("numpy", "numpy_version"),
    "scipy": ("scipy", "scipy_version"),
    "soundfile": ("soundfile", "soundfile_version"),
    "decoder": ("decoder", "decoder_id"),
    "resampler": ("resampler", "resampler_id"),
    "dsptap": ("dsptap_commit", "dsptap"),
    "contract": ("contract_version", "log_mel_contract_version"),
    "kws_features": ("kws_features_version",),
    "onnxruntime": ("onnxruntime", "onnxruntime_version"),
}
NOT_RECORDED = "not recorded in this build"
HERE = pathlib.Path(__file__).resolve().parent
# The synth hand-run record (plan §6 M4a): a lock excerpt — the TTS rows, toolchain and self-check of a real
# `synth` run over one pinned voice on the M0 Mac, no audio — cited when the lock's own build ran no synth.
HAND_RUN_RECORD = HERE / "fixtures" / "synth_hand_run" / "lock_excerpt.json"
HAND_RUN_MANIFEST = HERE / "fixtures" / "synth_hand_run" / "manifest.json"


class CardError(ValueError):
    """The card refuses to render; the message lists every offending clip and its rule."""


# ---------------------------------------------------------------- helpers


def hours(samples: int) -> float:
    return float(samples) / RATE / 3600.0


def fmt_hours(samples: int) -> str:
    return f"{hours(samples):.4f}"


def md_escape(text: Any) -> str:
    return str(text).replace("|", "\\|").replace("\n", " ")


def md_table(columns: Iterable[str], rows: Iterable[Iterable[Any]]) -> str:
    cols = list(columns)
    out = ["| " + " | ".join(md_escape(c) for c in cols) + " |", "|" + "---|" * len(cols)]
    for r in rows:
        out.append("| " + " | ".join(md_escape(c) for c in r) + " |")
    return "\n".join(out)


def effective_licence(clip: Clip, source: Source | None) -> str:
    per_file = clip.extra.get("licence")
    if isinstance(per_file, str) and per_file:
        return per_file
    return source.licence if source is not None else ""


def effective_attribution(clip: Clip, source: Source | None) -> str:
    per_file = clip.extra.get("attribution")
    if isinstance(per_file, str) and per_file:
        return per_file
    return source.attribution if source is not None else ""


def is_synthetic(clip: Clip) -> bool:
    return clip.material == "tts" or bool(clip.extra.get("synthetic", False))


def class_name(clip: Clip) -> str:
    if clip.label == 1:
        return "positive"
    if clip.label == 0:
        return "negative"
    return f"{clip.material} (augmentation material)"


def origin_text(source: Source) -> str:
    if source.origin.in_repository:
        return f"repository: {source.origin.path}"
    obtain = f" ({source.origin.obtain})" if source.origin.obtain else ""
    return f"{source.origin.url}{obtain}"


def yes_no(flag: bool) -> str:
    return "yes" if flag else "no"


def dry_clips(lock: Lock) -> list[Clip]:
    return sorted((c for c in lock.clips if c.variant == 0), key=lambda c: c.id)


def tool(toolchain: dict[str, Any], name: str) -> str:
    """The toolchain block's value for one of _TOOLCHAIN_KEYS' names, whichever spelling the build used."""
    for k in _TOOLCHAIN_KEYS[name]:
        if k in toolchain and toolchain[k] not in (None, ""):
            return str(toolchain[k])
    return NOT_RECORDED


# ---------------------------------------------------------------- the checks


def check_licences(manifest: Manifest, lock: Lock) -> list[str]:
    """Every licence failure across the lock's clips, as messages naming the clip and the rule."""
    sources = {s.id: s for s in manifest.sources}
    accounted = "(rule: every hour of audio is accounted for with a licence)"
    failures: list[str] = []
    for c in sorted(lock.clips, key=lambda c: (c.id, c.variant)):
        s = sources.get(c.source)
        if s is None:
            failures.append(f"clip {c.id}: its source {c.source!r} is not in the manifest "
                            "(rule: every clip maps to a licensed, role-tagged source)")
            continue
        if c.extra.get("unlicensed", False):
            failures.append(f"clip {c.id}: flagged unlicensed by its adapter {accounted}")
        lic = effective_licence(c, s)
        if not lic:
            failures.append(f"clip {c.id}: source {s.id!r} has an empty licence and the clip carries "
                            f"none of its own {accounted}")
        elif _NC_TOKEN.search(lic):
            failures.append(f"clip {c.id}: licence {lic!r} carries a non-commercial (NC) term "
                            "(rule: no licence id carrying a non-commercial NC term enters the corpus)")
    return failures


def check_lock_matches(manifest: Manifest, lock: Lock) -> list[str]:
    h = manifest_hash(manifest)
    if lock.manifest_hash != h:
        return [f"lock.manifest_hash {lock.manifest_hash} != the manifest's {h} "
                "(rule: the card describes the build of exactly this manifest)"]
    return []


def refuse_or_pass(manifest: Manifest, lock: Lock) -> None:
    failures = check_lock_matches(manifest, lock) + check_licences(manifest, lock)
    if failures:
        raise CardError(f"dataset card refused, {len(failures)} failure(s):\n  " + "\n  ".join(failures))


# ---------------------------------------------------------------- the tables


def source_rows(manifest: Manifest, lock: Lock) -> list[list[Any]]:
    dry = dry_clips(lock)
    rows = []
    for s in manifest.sources:
        mine = [c for c in dry if c.source == s.id]
        rows.append([s.id, s.kind, s.release, s.material, ", ".join(s.roles), len(mine),
                     fmt_hours(sum(c.length for c in mine)), s.licence, s.terms_verified,
                     yes_no(s.redistributable), yes_no(s.pcm_exact)])
    return rows


def split_class_rows(lock: Lock) -> list[list[Any]]:
    """Rows (split, class): dry and augmented counts / hours and totals, from the per-clip lengths."""
    acc: "OrderedDict[tuple[str, str], list[int]]" = OrderedDict()
    for c in sorted(lock.clips, key=lambda c: (c.split, c.id, c.variant)):
        cell = acc.setdefault((c.split, class_name(c)), [0, 0, 0, 0])
        if c.variant == 0:
            cell[0] += 1
            cell[1] += c.length
        else:
            cell[2] += 1
            cell[3] += c.length
    order = {"train": 0, "dev": 1, "eval": 2}
    rows = []
    for (split, cls), (n0, s0, n1, s1) in sorted(acc.items(), key=lambda kv: (order.get(kv[0][0], 9), kv[0])):
        rows.append([split, cls, n0, fmt_hours(s0), n1, fmt_hours(s1), n0 + n1, fmt_hours(s0 + s1)])
    return rows


def derived_class_balance(lock: Lock) -> list[dict[str, Any]]:
    """The class-balance table recomputed from the clips: the fallback when the lock carries none.

    The same vocabulary kws_build.stage_shard emits: `origin` real / synthetic, `render` dry / augmented
    (named `render`, not `condition`, because the plan's `condition` is the recording condition M4b adds).
    """
    acc: dict[tuple[Any, ...], list[int]] = {}
    for c in lock.clips:
        key = (c.source, "synthetic" if is_synthetic(c) else "real", "dry" if c.variant == 0 else "augmented",
               str(c.extra.get("keyword", "")), c.split, c.share, "" if c.label is None else int(c.label))
        cell = acc.setdefault(key, [0, 0])
        cell[0] += 1
        cell[1] += c.length
    rows = []
    for key in sorted(acc, key=lambda k: tuple(str(x) for x in k)):
        n, s = acc[key]
        rows.append({"source": key[0], "origin": key[1], "render": key[2], "keyword": key[3],
                     "split": key[4], "share": key[5], "label": key[6], "count": n, "hours": hours(s)})
    return rows


def class_balance_columns(rows: list[dict[str, Any]]) -> list[str]:
    preferred = ["source", "origin", "render", "keyword", "voice", "speaker", "split", "share", "label",
                 "count", "hours"]
    seen: set[str] = set()
    for r in rows:
        seen.update(r.keys())
    return [c for c in preferred if c in seen] + sorted(seen - set(preferred))


def class_balance_cell(row: dict[str, Any], column: str) -> Any:
    v = row.get(column, "")
    return f"{v:.4f}" if column == "hours" and isinstance(v, float) else v


def partition_rows(manifest: Manifest, lock: Lock) -> list[list[Any]]:
    """Which material from which source went to which share (and split): dry clips and hours."""
    acc: dict[tuple[str, str, str, str], list[int]] = {}
    for c in dry_clips(lock):
        cell = acc.setdefault((c.source, c.material, c.share, c.split), [0, 0])
        cell[0] += 1
        cell[1] += c.length
    order = {s.id: i for i, s in enumerate(manifest.sources)}
    rows = []
    for key in sorted(acc, key=lambda k: (order.get(k[0], len(order)), k)):
        n, s = acc[key]
        rows.append([key[0], key[1], key[2], key[3], n, fmt_hours(s)])
    return rows


def split_key_rows(manifest: Manifest) -> list[list[Any]]:
    return [[s.id, s.kind, SPLIT_KEY_RULE.get(s.kind, f"kind {s.kind!r}: no split-key rule recorded")]
            for s in manifest.sources]


def hand_run_record(path: pathlib.Path = HAND_RUN_RECORD) -> Lock | None:
    """The committed synth hand-run lock excerpt, or None when the repository carries none."""
    return read_lock(path) if path.is_file() else None


def hand_run_rows(record: Lock) -> list[list[Any]]:
    """The hand run's tool versions and its rows' voices, rendered from the record file."""
    t = record.toolchain
    voices = sorted({str(c.extra.get("voice", "?")) for c in record.clips if c.material == "tts"})
    n_tts = sum(1 for c in record.clips if c.material == "tts" and c.variant == 0)
    over = f"hand run over {', '.join(voices) or 'no voice'}: {n_tts} TTS clips"
    rows = [[name, tool_version(t, keys), licence, f"{role} ({over})"]
            for name, keys, licence, role in GPL_SUBPROCESS_TOOLS]
    rows.append(["ONNX Runtime", tool(t, "onnxruntime"), "MIT",
                 "Piper's inference engine, inside the piper-tts subprocess; never redistributed (hand run)"])
    rows.append(["DspTap (hand run)", tool(t, "dsptap"), "MIT",
                 "the front end the hand run's rows were featurized with (contract version "
                 f"{tool(t, 'contract')})"])
    return rows


def tool_version(t: dict[str, Any], keys: tuple[str, ...]) -> str:
    return next((str(t[k]) for k in keys if t.get(k) not in (None, "")),
                "not run in this build (no `tts` block or no synthesis)")


def tooling_rows(lock: Lock) -> tuple[list[list[Any]], list[str]]:
    """The build-time tooling table and the toolchain keys it did not consume."""
    t = lock.toolchain
    rows = [
        ["Python", tool(t, "python"), "PSF-2.0", "interpreter of the builder"],
        ["numpy", tool(t, "numpy"), "BSD-3-Clause", "arrays, the front end's double input"],
        ["scipy", tool(t, "scipy"), "BSD-3-Clause",
         "`scipy.signal.resample_poly`, the one resampler; RIR convolution"],
        ["soundfile", tool(t, "soundfile"), "BSD-3-Clause",
         "decoding through libsndfile (LGPL-2.1-or-later, dynamically loaded; build-time only)"],
        ["decoder id", tool(t, "decoder"), "—", "the pcm tier's decoder key"],
        ["resampler id", tool(t, "resampler"), "—", "the pcm tier's resampler key"],
        ["DspTap", tool(t, "dsptap"), "MIT",
         f"the shipping front end through its C ABI (log_mel contract version {tool(t, 'contract')}, "
         f"kws_features version {tool(t, 'kws_features')})"],
    ]
    used: set[str] = set()
    for keys in _TOOLCHAIN_KEYS.values():
        used.update(keys)
    for name, keys, licence, role in GPL_SUBPROCESS_TOOLS:
        rows.append([name, tool_version(t, keys), licence, role + GPL_ROW_SUFFIX])
        used.update(keys)
    if tool(t, "onnxruntime") != NOT_RECORDED:
        rows.append(["ONNX Runtime", tool(t, "onnxruntime"), "MIT",
                     "Piper's inference engine, inside the piper-tts subprocess; never redistributed"])
    return rows, sorted(k for k in t if k not in used)


def self_check_records(lock: Lock) -> list[dict[str, Any]]:
    sc = lock.self_check
    if isinstance(sc, list):
        return [r for r in sc if isinstance(r, dict)]
    if isinstance(sc, dict) and "geometry" in sc:
        return [sc]
    if isinstance(sc, dict):
        return [r for r in sc.values() if isinstance(r, dict) and "geometry" in r]
    return []


def geometry_matches(a: Any, b: dict[str, Any]) -> bool:
    """Same log_mel values apart from the PCEN block (the self-check runs both paths itself)."""
    if not isinstance(a, dict):
        return False
    return {k: v for k, v in a.items() if k != "pcen"} == {k: v for k, v in b.items() if k != "pcen"}


def self_check_rows(lock: Lock, manifest_geometry: dict[str, Any]) -> tuple[list[list[Any]], bool]:
    """The self-check table and whether any record ran at the manifest geometry."""
    rows = []
    at_manifest_any = False
    for rec in self_check_records(lock):
        gg = rec.get("geometry", {})
        at_manifest = geometry_matches(gg, manifest_geometry)
        at_reference = geometry_matches(gg, REFERENCE.to_dict())
        at_manifest_any = at_manifest_any or at_manifest
        which = ("reference = manifest" if at_manifest and at_reference else "manifest" if at_manifest
                 else "reference" if at_reference else "other")
        if isinstance(gg, dict):
            gtxt = (f"{gg.get('sample_rate', '?')} Hz / {gg.get('frame', '?')} / {gg.get('hop', '?')} / "
                    f"{gg.get('fft_size', '?')} / {gg.get('bands', '?')} bands")
        else:
            gtxt = str(gg)
        rows.append([which, gtxt, rec.get("tolerance", ""), rec.get("max_abs_diff_log", ""),
                     rec.get("max_abs_diff_pcen", "")])
    return rows, at_manifest_any


# ---------------------------------------------------------------- rendering


def render_card(manifest: Manifest, lock: Lock) -> str:
    """DATASET.md as text. Raises CardError (after checking) rather than rendering an unlicensed corpus."""
    refuse_or_pass(manifest, lock)
    r = manifest.recipe
    dry = dry_clips(lock)
    out: list[str] = []
    p = out.append

    p(f"# Dataset card — `{manifest.name}`")
    p("")
    p("Generated by `tools/ml/kws/kws_dataset_card.py` from `manifest.json` and the build's")
    p("`lock.json`; nothing here is typed by hand. Regenerate after any build; the file is a pure")
    p("function of its inputs.")
    p("")
    p(md_table(["field", "value"], [
        ["manifest", manifest.name],
        ["manifest version", manifest.manifest_version],
        ["manifest hash (sources[] + recipe)", lock.manifest_hash],
        ["lock version", lock.lock_version],
        ["sources", len(manifest.sources)],
        ["dry clips", len(dry)],
        ["augmented variants", sum(1 for c in lock.clips if c.variant != 0)],
        ["dry hours", fmt_hours(sum(c.length for c in dry))],
        ["augmented hours", fmt_hours(sum(c.length for c in lock.clips if c.variant != 0))],
        ["shards", len(lock.shards)],
    ]))
    p("")

    p("## Sources, hours and licences")
    p("")
    p("Every clip in the lock maps to one of these rows; the licence checks below refuse the card")
    p("otherwise. Hours are decoded durations from the lock's per-clip sample counts at 16 kHz (dry")
    p("clips only).")
    p("")
    p(md_table(["source", "kind", "release", "material", "roles", "clips", "hours", "licence",
                "terms verified", "redistributable", "pcm exact"], source_rows(manifest, lock)))
    p("")
    p("Terms accepted on download, per source:")
    p("")
    for s in manifest.sources:
        p(f"- `{s.id}` ({origin_text(s)}): {s.terms_accepted} — verified {s.terms_verified}.")
    p("")

    committed = [s for s in manifest.sources if s.origin.in_repository]
    if committed:
        p("### Committed material (the toy set's provenance)")
        p("")
        p("These sources are committed under the repository (`origin.path`) and are therefore")
        p("redistributable by the manifest's own rule — an origin inside the repository requires")
        p("`redistributable: true`, and the builder refuses the manifest otherwise. Their archives are")
        p("verified against the manifest's sha256 and size at `fetch` like every other source.")
        p("")
        p(md_table(["source", "path", "archive", "sha256", "size", "licence"],
                   [[s.id, s.origin.path, s.archive.file, s.archive.sha256, s.archive.size, s.licence]
                    for s in committed]))
        p("")

    p("## Licence checks")
    p("")
    p("All of the following held when this card was rendered (the script refuses to write it otherwise):")
    p("")
    p("- every clip's effective licence — its per-file `extra.licence` where the source carries")
    p("  per-file attribution, else its source's `licence` — is non-empty;")
    p("- no clip is flagged `unlicensed` by its adapter;")
    p("- no effective licence id carries a non-commercial (`NC`) term;")
    p("- every clip's source is a manifest row, and the lock's manifest hash is this manifest's.")
    p("")
    p(f"`{ATTRIBUTION_NAME}` beside this file carries one row per source and one per clip with per-file")
    p("attribution; the M6 exporter's provenance block and the M8 notices derive from it.")
    p("")

    p("## Phrase")
    p("")
    p(f"- phrase: `{r.phrase}`")
    near = ", ".join(f"`{w}`" for w in r.near_miss) if r.near_miss else "none"
    p(f"- near-miss keywords (negatives flagged `near_miss`): {near}")
    p(f"- mining: {', '.join(f'{k} = {v}' for k, v in sorted(r.mining.items()))}")
    p(f"- exclusions: {', '.join(f'{k} = {v}' for k, v in sorted(r.exclusions.items()))}")
    p("- the phrase is a development phrase: never shipped (plan M0).")
    p("")

    p("## Splits: hours and counts per split and class")
    p("")
    p("From the lock's per-clip sample counts. Dry = variant 0; augmented = the frozen draws.")
    p("")
    p(md_table(["split", "class", "dry clips", "dry hours", "augmented clips", "augmented hours",
                "total clips", "total hours"], split_class_rows(lock)))
    p("")

    p("## Class balance")
    p("")
    if lock.class_balance:
        balance = lock.class_balance
        p("The lock's class-balance table, verbatim.")
    else:
        balance = derived_class_balance(lock)
        p("The lock carries no class-balance table; this one is recomputed from its clips (source,")
        p("real / synthetic, dry / augmented, keyword, split, share, label).")
    p("")
    cols = class_balance_columns(balance)
    p(md_table(cols, [[class_balance_cell(row, c) for c in cols] for row in balance]))
    p("")

    p("## Partition: which material went where")
    p("")
    p("Dry clips and hours per (source, material, share, split). Eval shares are never trained on and")
    p("never augment training or dev; the eval noise share exists for the hold-out's SNR mixing.")
    p("")
    p(md_table(["source", "material", "share", "split", "clips", "hours"], partition_rows(manifest, lock)))
    p("")
    if lock.eval_set_id:
        p("Eval set ids (sha256 of the sorted clip ids of the share — a pure function of `sources[]` and")
        p("`recipe`, so a change is a manifest-hash change):")
        p("")
        p(md_table(["share", "eval_set_id"], [[k, v] for k, v in sorted(lock.eval_set_id.items())]))
        p("")
    if lock.holdout_set_id:
        p(f"Hold-out set id: `{lock.holdout_set_id}`")
    else:
        p("Hold-out set id: none (no `holdout` source in this manifest).")
    p("")

    p("## Split rule")
    p("")
    fractions = ", ".join(f"{k} = {v}" for k, v in r.split.fractions.items())
    p("Assignment is by rule per source — a keyed hash (`kws_audio.assign_split`) with the committed")
    p(f"salt `{r.split.salt}` and fractions {fractions} — or a source's official split where one")
    p("exists. The key per source:")
    p("")
    p(md_table(["source", "kind", "split key"], split_key_rows(manifest)))
    p("")
    p("**Client-id approximation.** The splits are client-id-disjoint and clip-id-disjoint per source")
    p("by the keys above. That is an approximation of speaker-disjoint: a person with two client ids")
    p("(or two Speech Commands speaker hashes) can appear on both sides. No demographic column (age,")
    p("gender, accent) is copied from any source; the split key is the only per-person field held.")
    p("`verify_splits.py` enforces the five rules R1–R5 over this lock.")
    p("")

    p("## Labels")
    p("")
    p("For every positive the keyword end sample *e* in the dry source is the end of the last")
    p(f"{r.label.window_ms:g} ms window within {r.label.trim_db:g} dB of the loudest, propagated")
    p("analytically through speed, RIR onset and stream offset (never re-trimmed after augmentation)")
    p(f"and stored as a 16 kHz sample index; its hop index is ⌊*e*/{r.geometry.hop}⌋. Tolerance")
    p(f"*T* = {r.label.tolerance_hops} hops (manifest value; a target until the full build measures the trim")
    p("rule's spread across the length-scale draws, M4b); M5's hit window is [*h* − *T*, *h* + 20 + *T*].")
    p("")

    p("## Augmentation")
    p("")
    a = r.augment
    p(f"Frozen into the store under seed {a.seed}; every draw is a function of the seed and the clip id")
    p("alone (`kws_audio.draw_rng`), so the lock does not depend on `--jobs`.")
    p(f"*K* = {a.draws_per_positive} draws per positive, one clean plus *M* = {a.noisy_per_negative} noisy")
    p(f"per negative, dry share {a.dry_share}; ranges: SNR {list(a.snr_db)} dB, gain {list(a.gain_db)} dB,")
    p(f"speed {list(a.speed)}, RT60 {list(a.rt60_s)} s, microphone model `{a.mic_model}`. Positives are")
    p(f"featurized embedded in {r.context_s:g} s of same-split negative material with one `reset()` per")
    p("mixture; a positive's RIR and SNR noise are applied over the whole mixture (context and keyword),")
    p("a negative's over its whole clip. Eval clips are never augmented.")
    p("")

    p("## Resampler")
    p("")
    win = ", ".join(str(v) for v in r.resampler.window)
    p(f"One resampler for every source and for TTS output: `{r.resampler.name}` with window ({win}),")
    p(f"scipy {tool(lock.toolchain, 'scipy')} (the lock's toolchain), resampler id")
    p(f"`{tool(lock.toolchain, 'resampler')}`. The shipping `decimate.h` is never in the training path.")
    p("")

    p("## Front end and self-check")
    p("")
    g = r.geometry
    p("Features come from the shipping front end (DspTap `log_mel.h` through its C ABI) at the manifest")
    p(f"geometry — {g.sample_rate:g} Hz, frame {g.frame}, hop {g.hop}, FFT {g.fft_size}, {g.bands} bands")
    p(f"{g.fmin_hz:g}–{g.fmax_hz:g} Hz, window `{g.window}`, stored path `{r.stored_path}` — computed in")
    p(f"double and stored float32, under log-mel contract version {r.log_mel_contract_version} and")
    p(f"kws_features version {r.kws_features_version}. DspTap commit: `{tool(lock.toolchain, 'dsptap')}`.")
    p("")
    rows, at_manifest = self_check_rows(lock, g.to_dict())
    if rows:
        p("The bridge was checked against the numpy oracle (`make_frontend_reference.py`) on the")
        p("reference signal:")
        p("")
        p(md_table(["geometry", "rate / frame / hop / fft / bands", "tolerance", "max |Δ| log",
                    "max |Δ| pcen"], rows))
        p("")
        if not at_manifest:
            p("No self-check record ran at the manifest geometry: the reference cannot yet be")
            p("parameterized to it, so that geometry is covered by the bridge and DspTap's typed")
            p("`log_mel` battery alone.")
            p("")
    else:
        p("The lock carries no self-check record: the manifest geometry is covered by the bridge and")
        p("DspTap's typed `log_mel` battery alone.")
        p("")

    p("## Build-time tooling")
    p("")
    p("Versions from the lock's toolchain block. The GPL rows are build-time subprocess tools: never")
    p("imported by distributed code, never redistributed, their output not a covered work — the same")
    p("entry `THIRD_PARTY_NOTICES.md` carries so its \"nothing GPL-encumbered\" sentence stays true.")
    p("")
    rows, rest = tooling_rows(lock)
    p(md_table(["tool", "pinned version", "licence", "role"], rows))
    p("")
    if rest:
        p("Other toolchain fields recorded by the build:")
        p("")
        p(md_table(["field", "value"], [[k, lock.toolchain[k]] for k in rest]))
        p("")
    synth_ran = any(k in lock.toolchain for k in GPL_SUBPROCESS_TOOLS[0][1])
    record = hand_run_record()
    if not synth_ran and record is not None:
        rel = HAND_RUN_RECORD.relative_to(HERE.parents[2]).as_posix()
        p("`synth` did not run in this build. The hand run the plan requires at M4a — `synth` over one")
        p("pinned voice on the M0 Mac, its lock rows carrying every per-clip TTS field and resolved draw,")
        p("the resampler record included, audio in the store and never in git — is recorded in")
        p(f"`{rel}` (manifest hash `{record.manifest_hash}`); its tool versions, rendered from that file:")
        p("")
        p(md_table(["tool", "version", "licence", "role"], hand_run_rows(record)))
        p("")

    p("## Shards")
    p("")
    if lock.shards:
        p(md_table(["split", "file", "clips", "frames", "sha256"],
                   [[s.split, s.file, s.clips, s.frames, s.sha256]
                    for s in sorted(lock.shards, key=lambda s: (s.split, s.file))]))
    else:
        p("No shards in this lock.")
    p("")

    p("## Hold-out: repository privacy statement")
    p("")
    p("Distinct from the runtime privacy statement (plan §5). The hold-out talkers' audio stays out of")
    p("git: it lives as FLAC under `<store>/holdout/` or as a private release asset. The repository")
    p("commits only the record, `holdout.json`. A row there holds, per utterance: the sha256 of the FLAC")
    p("file, the talker's pseudonym, the microphone path, distance, SNR, phrase, endpoint sample and")
    p("consent-form version — no audio, no form, no name. A hash reveals nothing without the file it")
    p("was computed from: it cannot be inverted, and it identifies a recording only to someone who")
    p("already holds that recording. Per talker the record holds a pseudonym, the consent-form version")
    p("and the permitted uses; signed forms live off-repo, referenced by pseudonym and form version.")
    p("")
    p("**Withdrawal.** A talker withdraws by request; the files are deleted from the store and any")
    p("release asset, their rows are dropped and `holdout.json` re-versioned, the `holdout` source's")
    p("sha256 (the hold-out set id, the hash of the sorted per-utterance FLAC hashes) changes and with")
    p("it the manifest hash, and every figure measured against the old set id is re-measured, never")
    p("edited. **History limit:** the withdrawn rows — hashes and conditions only, never audio — stay")
    p("readable in the repository's history and in forks; the consent template says so.")
    p("")
    holdout_sources = [s for s in manifest.sources if s.kind == "holdout"]
    if holdout_sources:
        p(md_table(["source", "hold-out set id", "terms verified"],
                   [[s.id, s.archive.sha256, s.terms_verified] for s in holdout_sources]))
    else:
        p("This manifest carries no `holdout` source (M4c).")
    p("")
    return "\n".join(out).rstrip("\n") + "\n"


def attribution_rows(manifest: Manifest, lock: Lock) -> list[list[str]]:
    """One row per source, plus one per dry clip carrying per-file attribution or licence in Clip.extra."""
    dry = dry_clips(lock)
    rows: list[list[str]] = []
    for s in manifest.sources:
        rows.append([s.id, "", s.kind, s.release, s.material, s.licence, s.attribution, s.terms_accepted,
                     s.terms_verified, yes_no(s.redistributable), origin_text(s), " ".join(s.roles)])
        for c in dry:
            if c.source != s.id or not (c.extra.get("attribution") or c.extra.get("licence")):
                continue
            rows.append([s.id, c.id, s.kind, s.release, c.material, effective_licence(c, s),
                         effective_attribution(c, s), s.terms_accepted, s.terms_verified,
                         yes_no(s.redistributable), origin_text(s), c.share])
    return rows


def render_attribution(manifest: Manifest, lock: Lock) -> str:
    refuse_or_pass(manifest, lock)
    buf = io.StringIO()
    w = csv.writer(buf, lineterminator="\n")
    w.writerow(ATTRIBUTION_COLUMNS)
    for row in attribution_rows(manifest, lock):
        w.writerow(row)
    return buf.getvalue()


def render(manifest: Manifest, lock: Lock, out_dir: pathlib.Path) -> tuple[pathlib.Path, pathlib.Path]:
    """Write DATASET.md and ATTRIBUTION.csv under out_dir; refuses (writes nothing) on any failure."""
    card = render_card(manifest, lock)
    attribution = render_attribution(manifest, lock)
    out_dir.mkdir(parents=True, exist_ok=True)
    card_path = out_dir / CARD_NAME
    attr_path = out_dir / ATTRIBUTION_NAME
    card_path.write_text(card, encoding="utf-8")
    attr_path.write_text(attribution, encoding="utf-8")
    return card_path, attr_path


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Render DATASET.md and ATTRIBUTION.csv from a manifest + lock")
    ap.add_argument("--manifest", required=True, help="manifest.json")
    ap.add_argument("--lock", required=True, help="the build's lock.json")
    ap.add_argument("--out-dir", default=str(pathlib.Path(__file__).resolve().parent),
                    help="where DATASET.md and ATTRIBUTION.csv go (default: tools/ml/kws)")
    ap.add_argument("--check", action="store_true", help="run the licence checks only; write nothing")
    args = ap.parse_args(argv)
    try:
        try:  # the inputs: a mistyped path or an unreadable file is a refusal, not a traceback
            manifest = load_manifest(args.manifest)
            lock = read_lock(args.lock)
        except ManifestError:
            raise
        except (OSError, ValueError, KeyError) as e:
            raise CardError(f"cannot read the inputs: {e}") from None
        if args.check:
            refuse_or_pass(manifest, lock)
            print(f"dataset card: {len(lock.clips)} clip rows over {len(manifest.sources)} source(s) pass "
                  "the licence checks")
            return 0
        card_path, attr_path = render(manifest, lock, pathlib.Path(args.out_dir))
    except (CardError, ManifestError) as e:  # anything else is a programming error and keeps its traceback
        print(f"kws_dataset_card: {e}", file=sys.stderr)
        return 1
    print(f"wrote {card_path} and {attr_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
