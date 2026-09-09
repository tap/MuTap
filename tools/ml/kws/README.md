# tools/ml/kws — the keyword-spotter dataset builder (M4)

The corpus, splits and dataset builder of the wake-word plan
(`docs/wake-word-plan.md` §6 M4, rev 3). This directory is the **M4a** stage:
the builder and its contracts on a redistributable bring-up corpus (Speech
Commands v2) and a committed toy fixture that CI rebuilds. M4b adds the full
corpus, M4c the recorded hold-out. Every number the plan states as a rule is
enforced here by a script that can fail.

Host-side tooling only: never part of the library or the emulated-target
builds. Python ≥ 3.10 in a pinned environment (`requirements.txt`; on the
M0 Mac `uv venv --python 3.12 .venv && uv pip install -r tools/ml/kws/requirements.txt`),
plus the DspTap C ABI, which `dsptap_py` builds on first import.

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
| `id` | stable directory name in the store, e.g. `speech_commands_v0.02` |
| `kind` | the ingestion adapter: `speech_commands_v2`, `musan`, `openslr_28_simulated`, `mswc_keywords`, `holdout` (M4b adds `common_voice`, `ami`, `fma`, `piper`) |
| `release` | the upstream release id (or Data Collective dataset id) |
| `origin` | `{"url": ..., "obtain": "<human step>"}` for a store-only source, or `{"path": "tools/ml/kws/fixtures/..."}` for a committed one |
| `archive` | `{"file", "sha256", "size"}` — `fetch` verifies both |
| `licence`, `attribution`, `terms_accepted`, `terms_verified` | the licence id, the attribution text, the terms accepted on download and the date they were verified |
| `redistributable` | may audio from this source enter git? An origin inside the repository requires `true`; the builder refuses the manifest otherwise |
| `roles` | subset of `train`, `aug`, `dev`, `eval-speech`, `eval-music`, `eval-noise`, `eval-tts`, `holdout` |
| `pcm_exact` | `true` when the archive is 16 kHz PCM, so the decoded-PCM sha256 is an identity field |
| `options` | adapter-specific: MUSAN's `partition`, the keyword-list member, … |

**`recipe`** — geometry values and stored path (`log` or `pcen`),
`log_mel_contract_version` (checked against `dsptap_log_mel_contract_version()`
at build) kept separate from `kws_features_version`, `phrase` and `near_miss`,
the label rule (`trim_db` *X*, `window_ms`, `tolerance_hops` *T*), `context_s`,
the resampler (name and window), the split rule (committed `salt`, `fractions`),
the augmentation policy (`seed`, `draws_per_positive` *K*, `noisy_per_negative`
*M*, `dry_share`, ranges), the exclusion rules, the mining method, and (M4b)
the TTS block and named subsets.

**`lock.json`** — emitted beside the shards. Per clip: id, source, material,
split, share, label, endpoint sample, length, split key, decoded-PCM sha256,
variant and resolved draw; per shard: file, sha256, frame count; per split ×
class hours and counts; the class-balance table; `eval_set_id` per eval share
(sha256 of the sorted clip ids — a pure function of the manifest); the hold-out
set id; the toolchain; the self-check record. **Identity fields** (everything
above except the next list) reproduce exactly on every rebuild. **Derived
fields** — feature values, per-shard sha256, the decoded-PCM sha256 of any
lossy or resampled source, the toolchain block — reproduce exactly only on the
M0 Mac in the pinned environment; elsewhere features agree within the
tolerance written beside the assertion and the rest are reported, never
compared.

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
  hand-exercised at M4a over one pinned voice, its lock rows kept as the record).
- `augment` freezes the augmentation into the store under the manifest seed.
  Every draw is a function of the seed and the clip id alone (`kws_audio.draw_rng`),
  never of a process-shared RNG, so the lock does not depend on `--jobs`.
- `extract` runs the front end (double) and writes float32 shards; positives
  are featurized embedded in `context_s` of same-split negative material with
  one `reset()` per mixture.
- `shard` writes the lock and the shard headers (`log_mel_contract_version`,
  the DspTap commit, the full geometry, `kws_features_version`, the manifest hash).

## Labels

For every positive the keyword end sample *e* in the dry source: the end of
the last `window_ms` window above peak − *X* dB, propagated analytically —
*e*′ = round(*e*/*f*) under speed factor *f*, *e*″ = *e*′ + *d* with *d* the
RIR's first sample within 40 dB of its peak, *e*‴ = *e*″ + the stream offset —
and never re-trimmed after augmentation. Stored as a 16 kHz sample index; its
hop index is ⌊*e*/hop⌋. M5's hit window is frames [*h* − *T*, *h* + 20 + *T*].

## Splits — `verify_splits.py`

Assignment is by rule (a keyed hash with the committed salt) per source, or a
source's official split where one exists (Speech Commands). Rules, each with a
planted-violation fixture under `fixtures/planted/`:

- **R1** client-id disjointness across Common Voice and MSWC.
- **R2** no decoded-PCM sha256 of any dev, eval or hold-out clip in a training
  or augmentation pool of any source; no training or dev noise file in any
  hold-out mixture.
- **R3** TTS voice + speaker-id disjointness.
- **R4** AMI participant disjointness.
- **R5** music artist disjointness after MUSAN/FMA de-duplication.

## The card — `kws_dataset_card.py`

Renders `DATASET.md` and `ATTRIBUTION.csv` from the manifest and lock; nothing
in the card is typed by hand. Its test plants an unlicensed clip and a
CC BY-NC track and requires both to fail.

## Tests and CI

`test_kws.py` (unittest) rebuilds the toy fixture into an empty store and is
the `kws-dataset` CI job's body. Every M4a pass item of the plan is one test.

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
checkable and a refactor cannot re-roll the data under an unchanged manifest:

```
{"gain_db": float, "snr_db": float | null, "noise": "<clip id>" | null,
 "speed": float, "rir": "<clip id>" | null, "rir_delay": int,
 "context": ["<clip id>", ...], "offset": int}
```

`offset` is the sample at which the (speed-scaled, reverberated) clip starts
inside its context material; the stored endpoint is *e*‴ = round(*e*/speed) +
`rir_delay` + `offset`. A hold-out mixed take (M4c) draws `{"noise", "level_db"}`.
