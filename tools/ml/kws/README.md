# tools/ml/kws — the keyword-spotter dataset builder (M4)

The corpus, splits and dataset builder of the wake-word plan
(`docs/wake-word-plan.md` §6 M4, rev 3). This directory is the **M4a** stage:
the builder and its contracts on a redistributable bring-up corpus (Speech
Commands v2) and a committed toy fixture that CI rebuilds. M4b adds the full
corpus, M4c the recorded hold-out. Every number the plan states as a rule is
enforced here by a script that can fail.

Host-side tooling only: never part of the library or the emulated-target
builds. Python ≥ 3.12 in a pinned environment (`requirements.txt` — the plan's
floor is 3.10, but the pinned numpy 2.5.3 and scipy 1.18.1 require 3.12; on the
M0 Mac `uv venv --python 3.12 .venv && uv pip install -r tools/ml/kws/requirements.txt`),
plus the DspTap C ABI, which `kws_features.ensure_bridge` builds (Release) on
first import and keys to the submodule commit with a `.dsptap_commit` marker
beside the library, rebuilding after a pin move so the commit the lock and the
shard headers record is the one the library was compiled from.

## Ownership

- **Formulas** — DspTap's `log_mel.h`; its numpy restatement
  `submodules/dsptap/tools/reference/make_frontend_reference.py` is the oracle.
- **Values** — `kws_features.py`: the manifest's `log_mel_geometry` values as a
  frozen `Geometry`, driven through `dsptap_py.LogMel`. Training features come
  from the shipping front end, computed in double, stored float32. The
  reference is imported for the parity **self-check** and the **band-support**
  check only (decided 8 September 2026).

## The store

`--store DIR` or `MUTAP_KWS_STORE`, no default. Tiers:

| tier | contents | regenerable |
|---|---|---|
| `archives/<source-id>/` | the verified input archive and its `.verified` marker (sha256) | never deleted |
| `extracted/<source-id>/` | the archive's members, extracted with filtered names | yes |
| `pcm/<source-id>/<decoder>-<resampler>/` | decoded 16 kHz int16 WAV clips | yes; eval and hold-out tiers regenerated before any DET |
| `features/<manifest-hash>/<split>/` | float32 shards, `lock.json` beside them | yes |
| `holdout/` | the M4c FLAC masters (never in git) | never deleted |

The manifest hash covers `sources[]` and `recipe`; any change yields a new
`features/` directory and a shard whose embedded hash differs is refused.

## The manifest (`manifest.json`, version 1)

Two committed parts and one emitted part.

**`sources[]`** — one entry per input:

| field | meaning |
|---|---|
| `id` | stable directory name in the store, e.g. `speech_commands_v0.02`: a single path segment `[A-Za-z0-9._-]+` (it names store directories; anything else is refused) |
| `kind` | the ingestion adapter (`kws_sources.py`): `speech_commands_v2`, `musan`, `openslr_28_simulated`, `keyword_list` (a text file, one keyword per line, `options.member`; the toy's Speech Commands list), `mswc_keywords`, `piper` (a pinned voice archive — `<voice>.onnx` + `<voice>.onnx.json` placed by hand, verified by `fetch`; it lists no clips, `synth` produces them from `recipe.tts`, whose every voice names its `piper` source), `holdout` (M4c; refused at M4a) — M4b adds `common_voice`, `ami`, `fma` |
| `release` | the upstream release id (or Data Collective dataset id) |
| `origin` | `{"url": ..., "obtain": "<human step>"}` for a store-only source, or `{"path": "tools/ml/kws/fixtures/..."}` for a committed one (an in-repository origin must lie under `tools/ml/kws/fixtures/`) |
| `archive` | `{"file", "sha256", "size"}` — `fetch` verifies both; `file` is a bare file name inside `archives/<id>/` |
| `licence`, `attribution`, `terms_accepted`, `terms_verified` | the licence id, the attribution text, the terms accepted on download and the date they were verified |
| `redistributable` | may audio from this source enter git? An origin inside the repository requires `true`; the builder refuses the manifest otherwise |
| `roles` | subset of `train`, `aug`, `dev`, `eval-speech`, `eval-music`, `eval-noise`, `eval-tts`, `holdout`. A clip is first assigned a split (official, or keyed hash), then a share from the roles: `train` / `dev` featurize the clip in that split; `aug` makes it mix-in material (noise, RIRs, or speech/music the roles do not featurize) in whichever training-side split its key hashes into — never featurized, `label` null; the eval roles select the eval share by material (`eval-speech`, `eval-music`, `eval-noise`, `eval-tts`). A clip whose split the roles do not cover is excluded and counted in the lock's `summary.excluded_by_role` |
| `pcm_exact` | `true` when the archive is 16 kHz PCM, so the decoded-PCM sha256 is an identity field |
| `options` | adapter-specific: MUSAN's `partition` and `exclude` (file ids left out, e.g. the one unattributed sound-bible file), Speech Commands' `keywords` restriction, SLR28's `sizes`, the keyword-list `member`, … |

**`recipe`** — geometry values and stored path (`log` or `pcen`; it must be
the path the geometry computes, `pcen` iff `geometry.pcen.enabled`, and the
shard header's `stored_path` is derived from the geometry), `log_mel_contract_version`
(checked against `dsptap_log_mel_contract_version()` at build) kept separate
from `kws_features_version`, `phrase` and `near_miss`, the label rule (`trim_db`
*X*, `window_ms`, `tolerance_hops` *T* — an integer, a target until M4b's build
measures the trim rule's spread), `context_s` (≥ 2 s, the plan's minimum), the
resampler (name and window), the split rule (committed `salt`, `fractions`),
the augmentation policy (`seed`, `draws_per_positive` *K*, `noisy_per_negative`
*M*, `dry_share`, ranges — each `[lo, hi]` with `lo ≤ hi`, `speed` positive,
`rt60_s` non-negative — and `mic_model`, `none` only: no microphone-path model
exists at M4a, so any other value is refused rather than silently ignored),
the exclusion rules, the mining method, the TTS block (`voices[]` — `id`,
`source` naming a `piper` sources[] row, `onnx` and `config` members of that
archive, the onnx `sha256`, optional `speakers` —, `texts.positive` /
`texts.negative`, `positives_per_speaker`, the `length_scale` / `noise_scale` /
`noise_w` ranges; filled by M4b, exercised by hand at M4a) and (M4b) named
subsets. Every schema deviation — an unknown or missing field anywhere — is
refused by name at load.

**`lock.json`** — emitted beside the shards. Per clip: id, source, material,
split, share, label, endpoint sample, length, split key, decoded-PCM sha256,
variant and resolved draw; per shard: file, sha256, frame count; per split ×
class hours and counts; the class-balance table; `eval_set_id` per eval share
(sha256 of the sorted clip ids — a pure function of the manifest); the hold-out
set id; the toolchain; the self-check record. A row exists for every decoded
clip (variant 0) and for every draw (variant 1…). A row's `endpoint_sample` is
the endpoint inside the audio that row is featurized from: the dry *e* for a
dry-only row, *e*‴ for a featurized positive (eval positives included, since
they are embedded in context); `extra.endpoint_dry` always keeps *e*. A row's
`length` is the sample count of the audio its `pcm_sha256` digests — the dry
clip, or the rendered variant in the augmented tier (a positive's is the
gain + speed keyword, since its RIR and noise are applied to the mixture) —
context excluded; hours come from these; `extra.mixture_samples` records the
mixture. MUSAN clips carry their per-file `extra.attribution` block and
the licence id classified from it as `extra.licence`, which the card checks.
**Identity fields** (everything above except the next list) reproduce exactly
on every rebuild. **Derived fields** — feature values, per-shard sha256, the
decoded-PCM sha256 of any lossy or resampled source and of every augmented
variant (rendered through `resample_poly` / `fftconvolve`), the toolchain block
— reproduce exactly only on the M0 Mac in the pinned environment; elsewhere
features agree within the tolerance written beside the assertion and the rest
are reported, never compared (`kws_manifest.compare_locks`).

## The builder — `kws_build.py`

```
python3 tools/ml/kws/kws_build.py <stage> --manifest M --store DIR [--jobs N]
stages: fetch · decode · synth · augment · extract · shard · all
```

- `fetch` never downloads: a store-only source is verified where a human
  placed it under `archives/<id>/`; a repository-relative origin is copied
  there — both against the manifest's sha256 and size — and every later stage
  refuses an archive without a matching `.verified` marker.
- `decode` extracts (filtered members; no links, no absolute or `..` paths),
  decodes through `soundfile`, resamples to 16 kHz through
  `scipy.signal.resample_poly` (the one resampler for every source; the
  shipping `decimate.h` is never in the training path), and writes the pcm tier.
- `synth` runs `piper-tts` as a subprocess over the manifest's TTS block (M4b;
  hand-exercised at M4a over one pinned voice — `fixtures/synth_hand_run/`
  holds the manifest used and `lock_excerpt.json`, the TTS rows and toolchain
  of that run, no audio, which the card cites). Every voice's `.onnx`/`.json`
  come from its `piper` source's verified archive; rows carry the voice, its
  sha256, speaker id (`"0"` for a single-speaker voice), the length / noise /
  noise-w scales drawn from `draw_rng(seed, clip_id + "#tts", 0)`, the text
  variant and the resampler record; the lock's toolchain gains the piper-tts
  and ONNX Runtime versions read from piper's own interpreter (espeak-ng is
  embedded in piper ≥ 1.3 and reports no version of its own, which the lock
  says rather than inventing one).
- `augment` freezes the augmentation into the store under the manifest seed.
  Every draw is a function of the seed and the clip id alone (`kws_audio.draw_rng`),
  never of a process-shared RNG, so the lock does not depend on `--jobs`.
  Train/dev positives get *K* draws, train/dev negatives one clean row plus
  *M* draws, eval clips none. A draw is resolved in the fixed order dry?
  (probability `dry_share`: gain only) → gain → speed → RIR (the same-split
  `rir` pool inside `rt60_s`, RT60 by `kws_audio.rt60_t20` recorded per RIR
  as `extra.rt60_s`; an empty pool is refused, never skipped) → noise (the
  same-split noise share) → SNR, then for every positive its context and
  offset. A negative variant is rendered whole (gain, speed, RIR, noise); a
  positive variant's augmented-tier file carries gain and speed only, its RIR
  and noise being applied to the whole mixture by `extract`. Rendered audio
  goes to `features/<hash>/augmented/` (regenerable).
- `extract` runs the front end (double) and writes float32 rows; positives
  are featurized embedded in same-split negative material: a stream of the
  drawn `context` clips, the keyword replacing it from `offset` for its own
  length, then (20 + *T*) hops plus one frame of the stream so the hit window
  fits; the draw's RIR is then convolved over the whole mixture (the
  direct-path delay *d* is uniform, so *e*‴ is unchanged) and the draw's noise
  added over the whole mixture at the drawn SNR measured against the keyword's
  extent [`offset`, `offset` + length + RIR tail), so the background and the
  reverberation run continuously across context and keyword — no
  keyword-bounded burst marks the positives (a treated mixture is
  peak-normalised to 0.99 like every rendered variant; an eval positive's is
  the raw concatenation); one `reset()` per mixture; negatives alone after a
  reset. It refuses a mixture whose rows do not reach the stored endpoint's
  hop, and a draw whose noise crop or signal is silent (`kws_audio.mix_snr`
  refuses rather than returning the input unchanged).
- `shard` writes the lock and the shard headers (`log_mel_contract_version`,
  the DspTap commit, the full geometry, `kws_features_version`, the manifest hash).
  `extract` records the DspTap commit it ran under in the ledger and `shard`
  refuses to stamp another one over its rows. Every stage refuses to run out
  of order (a stage ledger in `features/<hash>/clips.json`), on an unverified
  archive (every stage after `fetch` re-checks every source's `.verified`
  marker), and under a `log_mel_contract_version` other than the bridge's.
  A clip that is not decodable audio, that decodes to digital silence, or that
  is shorter than one label window is refused by name at `decode`.

## Labels

For every positive the keyword end sample *e* in the dry source: the end of
the last `window_ms` window above peak − *X* dB, propagated analytically —
*e*′ = round(*e*/*f*) under speed factor *f*, *e*″ = *e*′ + *d* with *d* the
RIR's first sample within 40 dB of its peak, *e*‴ = *e*″ + the stream offset —
and never re-trimmed after augmentation. Stored as a 16 kHz sample index; its
hop index is ⌊*e*/hop⌋. M5's hit window is frames [*h* − *T*, *h* + 20 + *T*].

## Splits — `verify_splits.py`

Assignment is by rule (a keyed hash with the committed salt) per source, or a
source's official split where one exists (Speech Commands); RIRs split train /
dev only, by the fractions renormalized over the two (`kws_build.rir_split`).
Rules, each with a planted-violation fixture under
`fixtures/planted/<Rn-name>/` (`lock.json` + `expect.json` naming the rule and
the planted clips, written by `fixtures/planted/make_planted_fixtures.py`;
`verify_splits.py --self-test` requires each to be rejected by exactly its
rule):

- **R1** client-id disjointness across Common Voice and MSWC.
- **R2** no decoded-PCM sha256 of any dev, eval or hold-out clip in a training
  or augmentation pool of any source; no training or dev noise file in any
  hold-out mixture.
- **R3** TTS voice + speaker-id disjointness.
- **R4** AMI participant disjointness.
- **R5** music artist disjointness after MUSAN/FMA de-duplication.

## The card — `kws_dataset_card.py`

Renders `DATASET.md` and `ATTRIBUTION.csv` from the manifest and lock; nothing
in the card is typed by hand. Its checks (`--check` runs them alone) refuse a
clip without a licence, a clip flagged `unlicensed`, and any licence id
carrying an `NC` term — per file where the adapter recorded `extra.licence`;
SA and ND ids pass the gate and are stated per file (whether such material is
admitted corpus-wide is a plan-level policy the card does not decide). Its
test plants an unlicensed clip and a CC BY-NC track and requires both to
fail. The class-balance table's vocabulary is `origin` (real / synthetic) and
`render` (dry / augmented) in both the builder and the card (`render`, not
`condition`: the plan's `condition` is the recording condition M4b adds).
When the lock's build ran no `synth`, the tooling section cites the committed
hand-run record (`fixtures/synth_hand_run/lock_excerpt.json`) and renders its
tool versions from that file. The toy's card is committed under `fixtures/toy/expected/` (a pure
function of the committed lock; `test_kws.py` requires the match); M4b's full
corpus renders to `tools/ml/kws/DATASET.md` and `ATTRIBUTION.csv`.

## Manifests

`manifests/<name>.json` are the committed manifests of real corpora; their
locks live in the store (hundreds of megabytes) and their rendered cards are
committed beside them under `manifests/<name>/` as the record M5 and M6 cite.
`speech_commands_v2_bringup.json` is M5's bring-up corpus (built 9 September
2026; the numbers are in the plan's M4a Done record and in its card).

## Tests and CI

`test_kws.py` (unittest) is the M4a pass, one test per item, and the body of
the `kws-dataset` CI job (`python -m unittest discover -s tools/ml/kws -p
'test_*.py' -v`, which also runs `test_verify_splits.py` and
`test_dataset_card.py`). It rebuilds `fixtures/toy/manifest.json` into empty
temporary stores with `--jobs 4` and `--jobs 1` and compares them with the
committed expected outputs under `fixtures/toy/expected/`: `lock.json`,
`features/<split>/shard-0000.npz` and the card, produced by a real toy build
on the M0 Mac at the pinned DspTap commit (the lock's toolchain block says
which; the test reports the commit and shard digests, never compares them). The fixture itself is
cut by `fixtures/make_toy_fixture.py` from the store's verified upstream
archives, deterministically; `fixtures/toy/provenance.json` records the picks.

## Shard format

`features/<manifest-hash>/<split>/shard-NNNN.npz`, clips in sorted clip-id
order, a fixed number of clips per shard, so the layout does not depend on
`--jobs`:

| array | dtype / shape | meaning |
|---|---|---|
| `features` | float32 (rows, bands) | the front end's output, double computed, stored float32 |
| `clip_ids` | str (n) | `<source-id>/<relative path without extension>` (+ `#<variant>` for a draw) |
| `clip_offsets` | int64 (n + 1) | row range of each clip: `[offsets[i], offsets[i+1])` |
| `clip_labels` | int8 (n) | 1 positive, 0 negative |
| `clip_endpoints` | int64 (n) | hop index of the keyword end *within the clip's rows* (positives), −1 otherwise |
| `clip_variants` | int32 (n) | 0 dry, 1… augmented draws |
| `geometry` | float64 (18) | `kws_features.Geometry.as_array()` |
| `header` | str () | JSON: `log_mel_contract_version`, `kws_features_version`, `manifest_hash`, `dsptap_commit`, `stored_path`, `split` |

The trainer (M6) refuses a shard whose header or geometry differs from the
first one it reads, as `train_suppressor.py` refuses mixed geometries.

## The resolved draw

`Clip.draw` names everything the augmentation did to a clip, so a rebuild is
checkable and a refactor cannot re-roll the data under an unchanged manifest
(the draw schema and its order — dry?, gain, speed, RIR, noise, SNR, context,
offset — are fixed; where a treatment is applied, the keyword alone or the
whole mixture, is `augment`'s and `extract`'s business, above):

```
{"gain_db": float, "snr_db": float | null, "noise": "<clip id>" | null,
 "speed": float, "rir": "<clip id>" | null, "rir_delay": int,
 "context": ["<clip id>", ...], "offset": int}
```

`offset` is the sample at which the speed-scaled clip starts inside its
context material — drawn in [`context_s`, 1.5 · `context_s`] samples (both
ends inclusive: *n* + {0, …, *n* // 2} with *n* = `context_s` · 16000) for a
positive, 0 with an empty `context` for a negative; the stored endpoint is
*e*‴ = round(*e*/speed) + `rir_delay` + `offset`. `speed` is the rational
factor actually applied, `Fraction(draw).limit_denominator(1000)` as a float
(the rule `kws_audio.change_speed` resamples by), not the raw uniform draw, so
*e*′ is computed from the factor the audio underwent; the render asserts the
applied factor equals the recorded one. Every id names a variant-0 row of the
same split. `rir` and `noise` are null and `speed` 1.0 for a dry draw. The
noise crop's start inside `kws_audio.mix_snr` comes from its own generator,
`draw_rng(seed, clip_id + "#noise", variant)`, so the schema above is complete
without a `noise_start` field; a TTS clip's synthesis scales come from
`draw_rng(seed, clip_id + "#tts", 0)`, so the variant-0 key stays reserved for
the eval positive's context draw. A hold-out mixed take (M4c) draws
`{"noise", "level_db"}`.

## M5 — the evaluation harness

The harness of the wake-word plan's M5 (`docs/wake-word-plan.md` §6 M5 and
§7), built before any model so that M6 is measured by tooling that can fail
on its own. Host-side only, like the builder; the same pinned environment.

**Modules.**

- `kws_scoring.py` — the scoring semantics **as numbers**, checked against
  hand-computed values by its own self-check (`python kws_scoring.py`):
  `Scoring` (hop, rate, *T* = `tolerance_hops`, *L* = `LATENCY_CEILING_HOPS`
  = 20, *W* = `smoothing_hops`, *R* = `refractory_hops`), `Positive`,
  `smooth`, `decide` (the reference decision stage: an upward crossing of
  the smoothed score at least *R* hops after the previous event — *t* − last
  ≥ *R*, so a crossing exactly *R* hops after an event fires and one *R* − 1
  hops after it merges), `hits` (one
  per utterance, window [*h* − *T*, *h* + *L* + *T*] inclusive with
  *h* = ⌊*e* / hop⌋), `spurious`, `poisson_interval` (exact two-sided 95 %,
  chi-square form), `zero_event_bound` (ln 20 / *H*), `wilson_interval`.
  `kws.h` carries the same numbers at M6 and must match them.
- `kws_streams.py` — `Stream` (id, share, positives, `negative_samples`,
  subshare, eager `audio` or lazy `load`, `members`), `streams_from_lock`:
  every variant-0 eval positive becomes one stream whose audio is the row's
  mixture (`kws_build.mixture` over its draw — what `extract` featurized, so
  the stream has exactly the lock's `extra.frames` hops and the hit window
  fits); the negatives of each eval share, sorted by id, are packed into
  `<share>/stream-NNNN` streams of at most 60 s, a clip never split (an
  over-long clip stands alone). Hours per share = Σ `negative_samples` /
  16000 / 3600 — the decoded durations, the plan's denominator; positives
  never enter it. `validate_streams` decodes every stream once and refuses by
  name: no streams, duplicate ids, an unknown share, an empty stream, a
  `negative_samples` that is not an integer, a positive stream with hours or
  without positives or without a subshare, an endpoint at or beyond its
  audio, an `endpoint_hop` that disagrees with its sample, a hit window
  ending beyond the stream's hops, a positive id used twice (within a stream
  or across streams), a negative stream with positives or whose
  `negative_samples` ≠ its decoded length. Its per-stream rules are
  `validate_stream(stream, n_samples, scoring, seen_positives)`, which takes
  the decoded length, so the harness applies the same rules — one rule set,
  not a copy — to the audio it decodes for scoring. A `Scoring` whose hop,
  rate, *T* or *L* differs from the manifest is refused
  (`require_scoring_matches`): the lock's rows were cut for those.
- `kws_detectors.py` — the `Detector` contract (one float64 score in [0, 1]
  per completed hop; `frames_for(n, hop)` = n // hop, measured through the
  bridge); `BandEnergyBaseline` (the plan's trivial sanity detector: the
  mean over the mel bands whose centre lies in [300, 3000] Hz of the shipping
  front end's plain-log feature, clipped to [0, 1], PCEN forced off and
  recorded in `params`); `PlantedDetector` (the oracle's: zeros except
  planted (hop, score) pairs per stream id, refusing a hop beyond the stream).
- `kws_holdout.py` — M4c's `holdout.json` (version 1: talkers with
  `consent_form_version` and `permitted_uses`; utterances with a FLAC path
  under `<store>/holdout/`, sha256, talker, microphone path, distance, SNR,
  phrase, 16 kHz `endpoint_sample`), refused at load on any schema
  deviation (a path only in its canonical spelling — no `.`, `..` or empty
  segment — and one sha256 per row, so one file is one utterance under any
  spelling, case-folded included); `verify_holdout` refuses by file a FLAC
  that is missing or whose sha256 differs, a stray FLAC no row names, two
  rows resolving to one file, a talker row missing, without a consent form
  version, or whose uses lack `evaluation` or `m7-replay`; `holdout_set_id`
  (sha256 of the sorted FLAC hashes, each once); `streams_from_holdout`
  (one positive stream per utterance, share `positives`, subshare `holdout`;
  a FLAC that is not 16 kHz is refused, never resampled here).
- `kws_eval.py` — `evaluate(streams, detector, scoring, thresholds)`: every
  stream decoded exactly once by `score_streams`, which checks its accounting
  on that decode (`kws_streams.validate_stream`, so a hand-built stream
  cannot mis-account) and scores it, returning a `Scored` (the scores by id
  plus the sample counts) that `evaluate` takes as verified — re-running only
  the accounting rules against the recorded counts, no second decode; a bare
  dict of scores is re-checked in full. Per threshold `decide` → hits /
  spurious on the positive streams, events on the negative streams per
  share; hours = the integer sample sum per share divided once (bit-identical
  to `hours_per_share` and the lock's summary); `Report` with `to_dict` /
  `from_dict` (schema-key diffing through `kws_manifest.check_keys`, a wrong
  `report_version` refused) and `markdown()`. `default_thresholds` = 0, 1
  and the quantiles of the per-stream maximum smoothed score. Refused by
  name: a detector whose score count is not n // hop, a score that is not a
  real number (a complex or bool array is never cast) or lies outside
  [0, 1], a threshold outside [0, 1] or repeated, scores for a stream the
  list does not carry, and on the CLI a `--grid` of −1 or 1.

**The report** (`report.json` + `report.md`, version 1): the manifest name and
hash; `eval_set_id` per share from the lock (and `holdout_set_id`); the
scoring numbers; the detector name and `params`; the front end's
`log_mel_contract_version` and DspTap commit; hours per share with stream
counts and the negative-stream packing bound they were assembled with
(`max_stream_s`, printed as `packed at <= 60 s`); positives per subshare
(eval-speech / eval-tts / holdout); then one
row per threshold — recall with its Wilson 95 % interval, FRR, spurious
events, and **FA/h on speech, music, TTS speech and noise as separate
columns**, each cell `rate [Poisson lo, hi] (events in H)` and `<= ln 20 / H`
where a share saw no event — with the eval-tts recall printed beside the
hold-out recall as the plan's descriptive gap figure. A share the lock lacks
reads `absent`, never 0 FA/h.

**Running it.**

```
# the sweep: the lock's eval shares through a detector
python tools/ml/kws/kws_eval.py sweep --manifest M --lock L --store DIR \
    [--detector band-energy] [--thresholds 50] [--grid N] [--smoothing 10] \
    [--refractory 100] [--max-stream-s 60] --out DIR
# the hold-out: verified (hashes, consent rows), then scored — beside the
# lock's shares when --lock is given
python tools/ml/kws/kws_eval.py holdout --holdout holdout.json --manifest M \
    --store DIR [--lock L] --out DIR
# the accounting of a lock alone
python tools/ml/kws/kws_streams.py --manifest M --lock L --store DIR --validate
# the DET notebook (executed and committed; needs requirements-notebook.txt)
python tools/ml/build_kws_det_notebook.py --manifest tools/ml/kws/manifests/speech_commands_v2_bringup.json
```

`--grid N` adds N evenly spaced thresholds to the quantiles: the band-energy
baseline's maximum saturates at 1.0 on most streams, so its quantiles
collapse (5 distinct thresholds from 50 quantiles on the bring-up corpus).
`--max-stream-s` (default `kws_streams.DEFAULT_MAX_STREAM_S` = 60) is
recorded in the report because every negative stream is decided after a
fresh reset: the *t* = 0 rule and the refractory restart at each stream
boundary make every FA/h row depend on the packing by at most streams / *H*
per share — the θ = 0 row, 60.65 FA/h on the bring-up speech share; the
decision-stage part of it measures 12–40 FA/h on speech (0.6–3.1 % of the
baseline's figure) at thresholds 0.1–0.9 against a run that decides the
concatenated per-stream scores, 9 September 2026.

Measured 9 September 2026 on the M0 Mac, the bring-up corpus
(`speech_commands_v2_bringup`, 263,487 lock rows read in 1.7 s, the streams
assembled in 0.08 s): 412 streams — 195 eval-speech positives, 179
eval-speech negative streams = **2.9513 h** (bit-identical to the lock's own
`summary.splits.eval.negative.hours`; the plan's "3.0 h" counted the
positives' 0.053 h, which the denominator excludes; zero-event bound 1.015
FA/h) and 38 eval-noise streams = 0.5021 h (bound 5.97 FA/h; three MUSAN
files longer than 60 s stand alone, the longest 113 s); music and TTS
absent. `kws_eval.py sweep` 8.6 s wall (6.2 s scoring + sweep, one decode
per stream, 5 thresholds); with `--grid 21` 9.9 s (24 thresholds); the
baseline scores about 2,500 s of audio per second including decoding and
the accounting check, 8,400 s/s scoring alone; peak RSS 1.04 GB, the parsed
lock. `notebooks/kws_det.ipynb` executes in 14 s (44 thresholds; 30 s on a
first run, while matplotlib builds its font cache) and is 0.12 MB. The
baseline's best recall over the sweep is 34/195 at 1,977 FA/h on speech —
the sanity curve, as expected useless, not a pass.

**The pass** (`test_kws_eval.py`, `test_kws_streams.py`; part of the
`kws-dataset` CI job, 44 tests in 2 s): the planted-event oracle — synthetic
silent streams scored by `PlantedDetector`, the per-utterance hit map
asserted through the harness's own `decide` → `hits` path with events inside
a window, at its two inclusive edges and one hop outside each (one at the
lower edge and one below, two at the upper edge and two above, so a rigid
shift of the window of up to 13 hops changes the aggregates as well as the
map), negative streams totalling exactly 0.5 h with four planted events of
which two fall inside one refractory period (3 counted → 6.0 FA/h, Poisson
[1.237, 17.535]), the refractory boundary as a number (events exactly *R*
hops apart both fire, *R* − 1 merge: 2 → 8.0 FA/h and 1 → 4.0 FA/h in 0.25 h),
one hit per utterance when two events fall in one window, a share with zero
events reporting ln 20 / *H*, spurious events never in FA/h, every figure
asserted against a hand computation, and the `_self_check`s of
`kws_scoring` and `kws_detectors` run under `unittest`; the mis-accounted
variants (a wrong, non-integer or negative `negative_samples`, an endpoint
or window beyond the stream, duplicate stream ids, a positive id used twice
within or across streams, a positive stream with hours) each refused by name
by both the harness and `validate_streams`, plus NaN / short / out-of-range
/ complex scores handed to `evaluate`, thresholds outside [0, 1] or
repeated, and a stream edited after scoring; the report round trip (the
packing bound included) and its markdown carrying every figure with its
hours; the toy rebuilt into a temporary store and swept end to end through
the bridge as a library call and through `kws_eval.py sweep` (hours = the
pcm tier's decoded lengths, bit-identical to `hours_per_share` and the
lock's summary, positives = the eval positives, `eval_set_id` = the lock's,
`max_stream_s` recorded, `--grid -1` and `--grid 1` refused by name, the
positive's frame count = `extra.frames` and its features = the committed
shard's rows); the band-energy alignment on a planted 1 kHz burst (peak at
or before hop *e* // 160 + 1, the score at *h* ≥ 0.32 and at *h* + 1 ≥ 0.96,
at most 0.979 at *h* + 2, 0 from *h* + 3, measured); the hold-out record
over soundfile-written FLACs (verify passes, one flipped byte refused naming
the file, the talker / consent / uses refusals, the set id changing when a
row is dropped and unchanged by a duplicate row, a second spelling or a
second row with one sha256 refused at load, two rows resolving to one file
refused by `verify_holdout`, a 22.05 kHz FLAC refused by name, and
`kws_eval.py holdout` writing a report with the record's set id then refusing
the altered tier with nothing written); and the three CLIs refusing a
missing store with `refused:` and rc 2.
