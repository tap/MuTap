#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""Assemble and execute notebooks/kws_det.ipynb — the wake-word plan's M5 DET notebook (§6 M5, §7).

Same convention as tools/ml/build_ml_notebook.py: the committed notebook is a build product of this
script — edit THIS file, rerun, and the .ipynb is overwritten with fresh outputs. The notebook loads a
corpus from the feature store (never from git), assembles the evaluation streams from its lock, runs the
band-energy sanity baseline through the DspTap C ABI, sweeps the thresholds with `kws_eval`, renders the
committed report format and draws the DET (FRR against FA/h per share) with the zero-event bound marked.
It is the sanity curve the plan asks for at M5, not a pass: the operating point is M6's, measured on the
hold-out.

    .venv/bin/python tools/ml/build_kws_det_notebook.py \\
        --manifest tools/ml/kws/manifests/speech_commands_v2_bringup.json [--store DIR] [--grid 41]

Refuses to run without a store (`--store` or MUTAP_KWS_STORE) and a manifest whose lock exists under
`<store>/features/<manifest-hash>/lock.json` (or `--lock`). The manifest is recorded in the notebook by
its repository-relative path; the store reaches the kernel through MUTAP_KWS_STORE, so a committed
notebook never carries a personal path. Requires tools/ml/kws/requirements-notebook.txt on top of
requirements.txt (nbformat, nbclient, ipykernel, matplotlib).
"""
from __future__ import annotations

import argparse
import os
import pathlib
import sys
import time

import nbformat as nbf
from nbclient import NotebookClient

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
KWS = REPO_ROOT / "tools" / "ml" / "kws"
sys.path.insert(0, str(KWS))

import kws_scoring  # noqa: E402
from kws_manifest import load_manifest, manifest_hash  # noqa: E402
from kws_streams import DEFAULT_MAX_STREAM_S  # noqa: E402
from kws_store import ENV_VAR, Store, StoreError, resolve_store  # noqa: E402

DEFAULT_OUT = REPO_ROOT / "notebooks" / "kws_det.ipynb"
DEFAULT_GRID = 41         # evenly spaced thresholds added to the quantiles (the baseline saturates at 1)
DEFAULT_THRESHOLDS = 50   # the quantile count of kws_eval.default_thresholds


def scoring_text(scoring: kws_scoring.Scoring) -> str:
    """The scoring semantics as a text cell, every number quoted from kws_scoring and the manifest."""
    s = scoring
    return f"""## The scoring semantics, as numbers

Everything below is `tools/ml/kws/kws_scoring.py` (checked against hand-computed values by its own
self-check, which the next cell runs) with the manifest's tolerance filled in; the notebook restates
nothing.

- **Hop alignment.** A detector yields one score per completed front-end hop of {s.hop} samples at
  {s.sample_rate} Hz: score[*t*] belongs to frame *t*, complete when sample (*t* + 1) · {s.hop} − 1
  arrives (`log_mel.h`'s alignment). An endpoint at sample *e* has hop index *h* = ⌊*e* / {s.hop}⌋.
- **The hit window** of a positive is frames [*h* − *T*, *h* + *L* + *T*], both ends inclusive, with
  *T* = {s.tolerance_hops} hops (the manifest's `label.tolerance_hops`) and *L* = {s.latency_hops} hops
  (`kws_scoring.LATENCY_CEILING_HOPS`, the §7 detection-latency ceiling of
  {s.latency_hops * s.hop * 1000 // s.sample_rate} ms). One hit per utterance.
- **The reference decision stage.** The score is smoothed by a trailing moving average over
  *W* = {s.smoothing_hops} hops; an event fires at hop *t* when the smoothed score crosses the threshold
  upward (s[*t*] ≥ θ and s[*t* − 1] < θ, or *t* = 0 and s[0] ≥ θ) and at least
  *R* = {s.refractory_hops} hops have passed since the previous event (*t* − last ≥ *R*: a crossing
  exactly *R* hops after an event fires, one *R* − 1 hops after it merges into it) — the refractory
  period, under which false accepts merge. (`kws.h` carries these numbers at M6 and must match them.)
- **False accepts.** An event on a negative stream; FA/h = events / *H*, *H* the negative streams'
  decoded duration in hours, per share (speech, music, TTS speech and noise separately). Every FA/h
  carries *H* and the exact two-sided 95 % Poisson interval on the count (chi-square form); at zero
  events the one-sided 95 % upper bound is ln 20 / *H*. An event on a positive stream outside every hit
  window is *spurious*: reported, never in FA/h.
- **Recall** = hits / positives with its Wilson 95 % interval; FRR = 1 − recall. A share the lock lacks
  is reported as *absent*, never as 0 FA/h.
"""


def build_notebook(manifest_rel: str, lock_rel: str | None, grid: int, thresholds: int, smoothing: int,
                   refractory: int, max_stream_s: float, scoring: kws_scoring.Scoring) -> nbf.NotebookNode:
    nb = nbf.v4.new_notebook()
    nb.metadata.kernelspec = {"display_name": "Python 3", "language": "python", "name": "python3"}
    cells: list = []

    def md(s: str) -> None:
        cells.append(nbf.v4.new_markdown_cell(s))

    def code(s: str) -> None:
        cells.append(nbf.v4.new_code_cell(s))

    md(f"""# Wake word — the M5 evaluation harness on the bring-up corpus

The wake-word plan's M5 ([`docs/wake-word-plan.md`](../docs/wake-word-plan.md) §6) builds the
evaluation harness *before any model*: the scoring semantics defined as numbers, a threshold sweep, a
committed report format with false accepts per hour on speech, music and TTS speech separately, each
figure with its hours and its interval. This notebook is that harness run end to end through the
shipping front end (DspTap's C ABI, `kws_features.FrontEnd`) on the corpus a manifest names, with the
plan's **trivial band-energy baseline** as the detector.

It is the **sanity curve the plan asks for, not a pass**. The pass is
[`test_kws_eval.py`](../tools/ml/kws/test_kws_eval.py)'s planted-event oracle, which runs in CI; the
DET evaluation is deliberately not in CI (§7) and lives here, executed and committed, rebuilt by
[`tools/ml/build_kws_det_notebook.py`](../tools/ml/build_kws_det_notebook.py) whenever behaviour
changes. The operating point is M6's, measured on the hold-out.

Manifest: `{manifest_rel}`. The store comes from `MUTAP_KWS_STORE` (no default); nothing in the
corpus enters git.""")

    lock_expr = (f"STORE / {lock_rel!r}" if lock_rel
                 else "Store(STORE).features(manifest_hash(manifest)) / 'lock.json'")
    code(f'''import json, os, pathlib, subprocess, sys, time
import numpy as np

REPO = pathlib.Path.cwd().parent if pathlib.Path.cwd().name == "notebooks" else pathlib.Path.cwd()
sys.path.insert(0, str(REPO / "tools" / "ml" / "kws"))
import kws_detectors, kws_eval, kws_features, kws_scoring, kws_streams
from kws_manifest import load_manifest, manifest_hash, read_lock
from kws_store import ENV_VAR, Store, resolve_store

STORE = resolve_store(None)   # refuses without MUTAP_KWS_STORE: there is no default store
MANIFEST = REPO / {manifest_rel!r}
manifest = load_manifest(MANIFEST)
LOCK = {lock_expr}
t0 = time.perf_counter()
lock = read_lock(LOCK)
t_lock = time.perf_counter() - t0
if lock.manifest_hash != manifest_hash(manifest):
    raise RuntimeError(f"lock {{LOCK}} was built from manifest {{lock.manifest_hash}}, this manifest hashes "
                       f"to {{manifest_hash(manifest)}}")
print(f"manifest {{manifest.name}} ({{manifest_hash(manifest)[:12]}}), store {{STORE.name}} "
      f"(from ${{ENV_VAR}})")
print(f"lock: {{len(lock.clips)}} rows read in {{t_lock:.2f}} s; eval_set_id per share:")
for share, sid in sorted(lock.eval_set_id.items()):
    print(f"  {{share}}: {{sid}}")
print(f"front end: log_mel_contract_version {{kws_features.contract_version()}}, "
      f"DspTap {{kws_features.dsptap_commit()}}")''')

    md(scoring_text(scoring))

    code(f'''# the scoring numbers are built from the manifest (hop and rate from the geometry, T from the
# label rule); kws_streams refuses any other value, since the lock's rows were cut for these
g = manifest.recipe.geometry
scoring = kws_scoring.Scoring(hop=g.hop, sample_rate=int(g.sample_rate),
                              tolerance_hops=manifest.recipe.label.tolerance_hops,
                              smoothing_hops={smoothing}, refractory_hops={refractory})
print(scoring.to_dict())
e = 24000
h = scoring.endpoint_hop(e)
print(f"an endpoint at sample {{e}}: hop {{h}}, hit window {{scoring.hit_window(h)}}")
r = subprocess.run([sys.executable, str(REPO / "tools/ml/kws/kws_scoring.py")], capture_output=True,
                   text=True)
print(r.stdout.strip() or r.stderr.strip())
if r.returncode != 0:
    raise RuntimeError("kws_scoring self-check failed")''')

    md(f"""## The streams

`kws_streams.streams_from_lock` turns the lock's eval shares into the streams the detector scores after
a single reset, so the smoothing window and the refractory period run across clip boundaries as they do
in a room. Every variant-0 eval positive becomes one stream whose audio is the row's mixture — exactly
what `extract` featurized (context + keyword + the (L + T) hops the hit window needs). The negatives of
each share are sorted by id and packed into streams of at most {max_stream_s:g} s, a clip never split (a
clip longer than that stands alone). The hours denominator is the sum of the negative streams' decoded
lengths — the plan's rule — so the positives' audio never enters it. Every stream is decided after a
fresh reset, so the packing bound is provenance the report records (`max_stream_s`): the *t* = 0 rule and
the refractory restart at each stream boundary make every FA/h figure depend on it by at most
streams / *H* per share, the ceiling printed below and reached at θ = 0. The accounting of every stream
is checked against its decoded audio (`kws_streams.validate_stream`, one rule set) by the scoring pass
in the next section, which decodes each stream exactly once.""")

    code(f'''t0 = time.perf_counter()
streams = kws_streams.streams_from_lock(manifest, lock, Store(STORE), scoring, max_stream_s={max_stream_s})
t_assemble = time.perf_counter() - t0
hours = kws_streams.hours_per_share(streams, scoring)
positives = kws_streams.positives_per_subshare(streams)
counts = {{}}
for s in streams:
    counts[s.share] = counts.get(s.share, 0) + 1
print(f"{{len(streams)}} streams assembled in {{t_assemble:.2f}} s (audio is decoded lazily)")
print(f"positives per subshare: {{positives}}")
for share in kws_eval.NEGATIVE_SHARES:
    if share in hours:
        longest = max(s.negative_samples for s in streams if s.share == share) / scoring.sample_rate
        print(f"{{share}}: {{counts[share]}} streams, H = {{hours[share]:.4f}} h "
              f"(zero-event bound ln 20 / H = {{kws_scoring.zero_event_bound(hours[share]):.3f}} FA/h; "
              f"longest stream {{longest:.1f}} s; packed at <= {max_stream_s:g} s, so the per-stream reset "
              f"can add at most streams / H = {{counts[share] / hours[share]:.2f}} FA/h to any row)")
    else:
        print(f"{{share}}: absent from this lock")''')

    md("""## The band-energy baseline

`kws_detectors.BandEnergyBaseline`: the mean, over the mel bands whose centre lies in
[300, 3000] Hz, of the shipping front end's plain-log feature, clipped to [0, 1]. It has no notion
of the keyword — it is the plan's *trivial* detector, run so the harness's plumbing (alignment, hours,
intervals, the report) is exercised on real audio before a model exists. The log affine maps a band
energy of 1 to a feature of exactly 1 and is unbounded above; clips that exceed that level saturate the
baseline's clip to [0, 1], so its per-stream maximum sits at 1.0 on such streams. The next cell measures
how many, and the sweep below adds an even grid of thresholds because the quantiles of saturated maxima
collapse.""")

    code(f'''detector = kws_detectors.BandEnergyBaseline(g)
print(f"detector {{detector.name}}: bands {{detector.bands[0]}}..{{detector.bands[-1]}} "
      f"({{detector.params['band_centres_hz'][0]}}..{{detector.params['band_centres_hz'][-1]}} Hz), "
      f"stored path {{detector.params['stored_path']}}")
t0 = time.perf_counter()
scores = kws_eval.score_streams(streams, detector, scoring)   # decode, check the accounting, score: once
t_score = time.perf_counter() - t0
audio_s = sum(y.size for y in scores.values()) * scoring.hop / scoring.sample_rate
print(f"scored {{audio_s:.0f}} s of audio ({{audio_s / 3600:.3f}} h, positives included) in "
      f"{{t_score:.1f}} s: {{audio_s / t_score:.0f}} s of audio per second, decode included")
maxes = np.array([kws_scoring.smooth(y, scoring.smoothing_hops).max() for y in scores.values()])
q = np.quantile(maxes, [0.0, 0.1, 0.5, 0.9, 1.0])
print("per-stream maximum smoothed score, quantiles 0/10/50/90/100 %:", np.round(q, 4).tolist())
saturated = int((maxes >= 1.0).sum())
print(f"streams whose smoothed maximum reaches 1.0: {{saturated}} of {{maxes.size}} "
      f"({{100.0 * saturated / maxes.size:.0f}} %)")
quantile_thresholds = kws_eval.default_thresholds(scores, scoring, n={thresholds})
print(f"default_thresholds ({thresholds} quantiles) yields only {{len(quantile_thresholds)}} distinct "
      f"thresholds; the sweep adds a grid of {grid}")''')

    code(f'''grid = {{kws_eval.threshold_value(v) for v in np.linspace(0.0, 1.0, {grid})}}
thresholds = sorted(set(quantile_thresholds) | grid)
provenance = {{"manifest_name": manifest.name, "manifest_hash": lock.manifest_hash,
              "eval_set_id": dict(lock.eval_set_id), "holdout_set_id": lock.holdout_set_id,
              "max_stream_s": {max_stream_s},
              "front_end": {{"log_mel_contract_version": kws_features.contract_version(),
                            "dsptap_commit": kws_features.dsptap_commit()}}}}
t0 = time.perf_counter()
report = kws_eval.evaluate(streams, detector, scoring, thresholds, scores=scores, provenance=provenance)
report.wall_s = t_score + time.perf_counter() - t0
OUT = REPO / "build-kws-det"
json_path, md_path = kws_eval.write_report(report, OUT)
print(f"{{len(report.rows)}} thresholds; scoring + sweep {{report.wall_s:.1f}} s; report at "
      f"{{json_path.relative_to(REPO)}} and {{md_path.relative_to(REPO)}}")''')

    md("""## The report

The committed format (`kws_eval.Report.markdown`): the header names the manifest and its hash, the
detector, the scoring numbers, the front end's contract version and DspTap commit, the `eval_set_id`
per share, the hours per share with their stream counts and packing bound, and the positives per
subshare; then one row
per threshold — recall with its Wilson interval, FRR, spurious events, and FA/h **per share** with the
Poisson interval and the hours behind it (`<= ln 20 / H` where a share saw no event). A share this
lock lacks reads `absent`, and the eval-tts and hold-out recall columns stand beside each other as the
plan's descriptive gap figure.""")

    code('''from IPython.display import Markdown, display
display(Markdown(report.markdown()))''')

    md("""## The DET

False-rejection rate against false accepts per hour, one curve per share present, FA/h on a log axis.
Each point is one threshold; the horizontal bar is the exact Poisson 95 % interval on that share's
count. The dashed vertical line per share is the zero-event bound ln 20 / *H*: nothing to the left of
it is measurable on this many hours, and a threshold at which the share saw no event is drawn *at* the
bound with an open, left-pointing marker (its rate is ≤ that bound, not 0). Missing shares are named in
the title rather than drawn as empty curves.""")

    code('''import matplotlib.pyplot as plt

SHARE_STYLE = {   # colour follows the share, in the report's column order, never the rank
    "eval-speech": ("speech", "#2a78d6", "o"), "eval-music": ("music", "#eb6834", "s"),
    "eval-tts": ("TTS speech", "#1baf7a", "D"), "eval-noise": ("noise", "#eda100", "^"),
}
present = [sh for sh in kws_eval.NEGATIVE_SHARES if sh in report.hours]
absent = [SHARE_STYLE[sh][0] for sh in kws_eval.NEGATIVE_SHARES if sh not in report.hours]
fig, ax = plt.subplots(figsize=(9, 4.8), dpi=100)
for sh in present:
    label, colour, marker = SHARE_STYLE[sh]
    H = report.hours[sh]
    rows = [(r.threshold, r.recall.frr, r.shares[sh]) for r in report.rows if r.recall.frr is not None]
    seen = [(f.fa_per_hour, frr, f.interval) for _, frr, f in rows if f.events > 0]
    unseen = [(f.zero_event_bound, frr) for _, frr, f in rows if f.events == 0]
    bound = kws_scoring.zero_event_bound(H)
    if seen:
        x = np.array([v[0] for v in seen]); y = np.array([v[1] for v in seen])
        lo = np.array([v[2][0] for v in seen]); hi = np.array([v[2][1] for v in seen])
        order = np.argsort(x)
        ax.errorbar(x[order], y[order], xerr=[x[order] - lo[order], hi[order] - x[order]], fmt=marker + "-",
                    color=colour, ecolor=colour, elinewidth=0.8, capsize=2, lw=1.5, ms=5, alpha=0.9,
                    label=f"{label} (H = {H:.3g} h, {report.negative_streams[sh]} streams)")
    if unseen:
        ax.plot([v[0] for v in unseen], [v[1] for v in unseen], marker="<", ls="none", mfc="none", mec=colour,
                ms=8, label=f"{label}: no event (rate <= ln 20 / H)")
    ax.axvline(bound, color=colour, ls="--", lw=1, alpha=0.7)
    ax.annotate(f"{label}: ln 20 / H = {bound:.2f} FA/h", xy=(bound, 0.98), xytext=(4, 0),
                textcoords="offset points", rotation=90, va="top", ha="left", fontsize=8, color="#52514e")
ax.set_xscale("log")
ax.set_ylim(-0.02, 1.02)
ax.set_xlabel("false accepts per hour (log axis; bars: exact Poisson 95 % interval)")
ax.set_ylabel("false-rejection rate (1 - recall over every positive)")
title = f"DET — band-energy sanity baseline on {manifest.name}"
if absent:
    title += f" (absent: {', '.join(absent)})"
ax.set_title(title, fontsize=11)
ax.grid(True, which="both", color="#e6e5e0", lw=0.6)
ax.set_axisbelow(True)
for side in ("top", "right"):
    ax.spines[side].set_visible(False)
ax.legend(fontsize=8, loc="lower left")
fig.tight_layout()
plt.show()''')

    code('''# what the curve says, in the numbers it was measured with (nothing below is typed by hand)
best = min((r for r in report.rows if r.recall.recall is not None), key=lambda r: r.recall.frr)
speech = "eval-speech"
print(f"positives: {report.positives}; thresholds swept: {len(report.rows)}")
for sh in present:
    H = report.hours[sh]
    print(f"{SHARE_STYLE[sh][0]}: H = {H:.4f} h, zero-event bound {kws_scoring.zero_event_bound(H):.3f} FA/h")
print(f"the baseline's best recall over the sweep: {best.recall.hits}/{best.recall.positives} = "
      f"{best.recall.recall:.3f} [{best.recall.interval[0]:.3f}, {best.recall.interval[1]:.3f}] at threshold "
      f"{best.threshold!r}, where FA/h speech = {kws_eval.format_fa(best.shares.get(speech))}")
under_1000 = [r for r in report.rows if speech in r.shares and r.shares[speech].fa_per_hour < 1000.0]
if under_1000:
    top = max(under_1000, key=lambda r: r.recall.recall)
    print(f"below 1000 FA/h on speech the best recall is {top.recall.hits}/{top.recall.positives} at "
          f"threshold {top.threshold!r} ({kws_eval.format_fa(top.shares[speech])})")
zero = report.rows[0]
print(f"threshold {zero.threshold!r}: every stream fires once at hop 0 (the t = 0 rule), so FA/h speech = "
      f"{kws_eval.format_fa(zero.shares.get(speech))} is the stream count over H, and recall is "
      f"{zero.recall.hits}/{zero.recall.positives} with {zero.spurious} spurious events")
earliest = min(scoring.hit_window(p.endpoint_hop)[0] for s in streams if s.share == "positives"
               for p in s.positives)
print(f"the earliest hit window on this corpus starts at hop {earliest}: hop 0 lies inside "
      f"{'no' if earliest > 0 else 'a'} window, which is why recall there is {zero.recall.hits}")
print(f"the plan's floors are >= 20 h of speech and >= 20 h of music (M4b's): this corpus has "
      + ", ".join(f"{SHARE_STYLE[sh][0]} {report.hours[sh]:.3f} h" for sh in present))
print(f"streams whose smoothed maximum reaches 1.0: {saturated} of {maxes.size}")
top = [r for r in report.rows if r.threshold >= 0.95]
print(f"the saturated end of the sweep, thresholds >= 0.95 ({len(top)} rows): speech events "
      f"{[r.shares[speech].events for r in top if speech in r.shares]}, hits "
      f"{[r.recall.hits for r in top]} of {report.rows[0].recall.positives}")''')

    md("""## What this is, and what it is not

This is the **sanity curve** the plan's M5 asks for: the harness's plumbing — hop alignment, the hit
window, the refractory merge, the hours from decoded durations, the Poisson and Wilson intervals, the
per-share report — exercised end to end on real audio through the shipping front end. The pass is the
planted-event oracle in `test_kws_eval.py` (exact to the utterance, in CI); the curve above is *not* a
pass and *not* a performance claim. A band-energy mean has no notion of the keyword: its recall is what
loud speech at the right moment buys, and its false-accept rate is whatever the printed numbers above
say — read them, not the shape. Every figure in this cell's argument is printed by the code cell above
it, from `report`; nothing here is typed by hand.

Three things the curve makes visible that M6 inherits:

- **The hours.** The FA/h denominator is the eval negatives' decoded duration only; the positives'
  audio never counts. The hours per share and the zero-event bound ln 20 / *H* are printed above beside
  the plan's floors (≥ 20 h speech and ≥ 20 h music), which are M4b's: until they exist, no figure can
  distinguish a good detector from a lucky one below that bound.
- **The saturation.** The count of streams whose smoothed band-energy maximum reaches 1.0 is printed
  above; where most do, the quantile thresholds collapse and an even grid fills the sweep. A real
  spotter's scores are calibrated by its training and the quantiles spread on their own. Because the
  reference stage smooths with a trailing mean over *W* hops, the smoothed score reaches 1.0 only where
  all *W* frames sit at the baseline's clip, so the rows at the saturated end of the sweep (thresholds
  ≥ 0.95, their speech counts and hits printed above) select among fully saturated stretches and say
  nothing about a detector.
- **The threshold-0 row, and the packing.** Under the reference decision stage an event fires at hop 0
  when s[0] ≥ θ, and every stream is decided after a fresh reset, so θ = 0 fires exactly once per stream:
  FA/h = streams / *H*. Recall in that row is 0 wherever no hit window reaches hop 0 — the earliest window
  start on this corpus is printed above; a window includes hop 0 only when the endpoint lies within
  *T* + 1 hops of the stream start. streams / *H* is also the ceiling of what the negative-stream packing
  bound (`max_stream_s`, recorded in the report) can add to any FA/h row through the *t* = 0 rule and the
  refractory restart at each stream boundary. It is the rule, stated as a number, not a defect.

Absent negative shares read *absent* in the report and are named in the plot title; an absent hold-out
reads *absent* in the report's positives line. The report never prints 0 FA/h for hours that were not
scored. **The operating point is M6's**, chosen on dev, and its recall is measured on the recorded
hold-out alone (`kws_eval.py holdout`, which refuses a hold-out whose file hashes do not match
`holdout.json`); the eval-tts recall beside it is a descriptive gap figure, never a pass.""")

    nb.cells = cells
    return nb


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--manifest", required=True,
                    help="a manifest under the repository (recorded by its repository-relative path)")
    ap.add_argument("--store", default=None, help=f"the feature store (or ${ENV_VAR}); no default")
    ap.add_argument("--lock", default=None,
                    help="the lock to score (default <store>/features/<manifest-hash>/lock.json)")
    ap.add_argument("--grid", type=int, default=DEFAULT_GRID,
                    help=f"evenly spaced thresholds added to the quantiles (default {DEFAULT_GRID})")
    ap.add_argument("--thresholds", type=int, default=DEFAULT_THRESHOLDS,
                    help=f"quantile count of kws_eval.default_thresholds (default {DEFAULT_THRESHOLDS})")
    ap.add_argument("--smoothing", type=int, default=kws_scoring.Scoring.smoothing_hops, help="W in hops")
    ap.add_argument("--refractory", type=int, default=kws_scoring.Scoring.refractory_hops, help="R in hops")
    ap.add_argument("--max-stream-s", type=float, default=DEFAULT_MAX_STREAM_S,
                    help=f"the negative-stream packing bound (default {DEFAULT_MAX_STREAM_S:g}, "
                         "kws_streams.DEFAULT_MAX_STREAM_S; recorded in the report)")
    ap.add_argument("--out", default=str(DEFAULT_OUT), help=f"the notebook to write (default {DEFAULT_OUT})")
    ap.add_argument("--timeout", type=int, default=3600, help="per-cell execution timeout in seconds")
    args = ap.parse_args(argv)
    try:
        store = resolve_store(args.store)
        manifest_path = pathlib.Path(args.manifest).resolve()
        if not manifest_path.is_relative_to(REPO_ROOT):
            raise StoreError(f"manifest {manifest_path} is not under the repository {REPO_ROOT}; the "
                             "notebook records manifests by repository-relative path")
        manifest = load_manifest(manifest_path)
        scoring = kws_scoring.Scoring(hop=manifest.recipe.geometry.hop,
                                      sample_rate=int(manifest.recipe.geometry.sample_rate),
                                      tolerance_hops=manifest.recipe.label.tolerance_hops,
                                      smoothing_hops=args.smoothing, refractory_hops=args.refractory)
        lock_rel = None
        if args.lock:
            lock_path = pathlib.Path(args.lock).resolve()
            if not lock_path.is_relative_to(store):
                raise StoreError(f"lock {lock_path} is not under the store {store}")
            lock_rel = lock_path.relative_to(store).as_posix()
        else:
            lock_path = Store(store).features(manifest_hash(manifest)) / "lock.json"
        if not lock_path.is_file():
            raise StoreError(f"no lock at {lock_path}: build the manifest into the store first "
                             "(kws_build.py all), or pass --lock")
    except (StoreError, ValueError, OSError) as e:
        print(f"build_kws_det_notebook: {type(e).__name__}: {e}", file=sys.stderr)
        return 1
    nb = build_notebook(manifest_path.relative_to(REPO_ROOT).as_posix(), lock_rel, args.grid, args.thresholds,
                        args.smoothing, args.refractory, args.max_stream_s, scoring)
    os.environ[ENV_VAR] = str(store)   # the kernel inherits it; the notebook itself carries no store path
    t0 = time.perf_counter()
    client = NotebookClient(nb, timeout=args.timeout, kernel_name="python3",
                            resources={"metadata": {"path": str(REPO_ROOT / "notebooks")}})
    client.execute()
    wall = time.perf_counter() - t0
    out = pathlib.Path(args.out)
    nbf.write(nb, str(out))
    print(f"wrote {out} ({out.stat().st_size / 1e6:.2f} MB) after executing {len(nb.cells)} cells in "
          f"{wall:.1f} s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
