# MuTap AEC vs. other echo cancellers

How the MuTap AEC (`mutap::aec_chain` — raw FD-Kalman canceller +
coherence residual suppressor + comfort noise, exposed as `mutap.aec~`
and certified in [itu-compliance.md](itu-compliance.md)) stacks up
against the two echo cancellers most people can name: **WebRTC AEC3**
(the canceller in Chromium and most soft-phones) and **Speex MDF** (the
classic open-source multi-delay-block NLMS canceller).

It is measured in **both directions**, because that is the honest way to
answer "how do we compare":

1. **Their algorithms through our tests** — every canceller is reduced
   to one black-box contract (far-end + microphone → cleaned send) and
   run through identical echo paths, signals, and metrics
   (`bench/compare/`, `-DMUTAP_BUILD_COMPARE=ON`).
2. **Our algorithm through their test** — every canceller's output is
   scored by WebRTC/Microsoft's own metric, **AECMOS**, the neural MOS
   predictor from the ICASSP AEC Challenge (`tools/compare/aecmos_eval.py`).

Neither direction alone is enough. Direction 1 measures echo-cancellation
depth on a controlled linear path, where MuTap's design is strongest;
direction 2 measures perceptual quality with a metric someone else
trained, where a raw-dB advantage does not automatically survive. Read
both.

## Subjects

| Subject | What it is | Fair peer |
|---|---|---|
| `mutap` | The certified `aec_chain<double>` (FD-Kalman + suppressor + comfort noise), the config [itu-compliance.md](itu-compliance.md) proves | full-chain peer of `webrtc` |
| `mutap-f32` | The same chain at float32 — the number a deployment actually runs | the honest float32 peer of `webrtc` |
| `mutap-linear` | The raw `partitioned_fdkf` canceller **alone**, no post-filter | a linear-AEC reference point |
| `speex` | speexdsp `speex_echo` (MDF-NLMS), float/KISS build, int16 I/O | classic adaptive-filter baseline |
| `webrtc` | webrtc-audio-processing `AudioProcessing` with the echo canceller enabled, `mobile_mode` off (**AEC3**), everything else disabled | full-chain, post-suppressor |

AEC3's output is post-suppressor (its linear filter, residual echo
suppressor and comfort noise are one unit with no public linear-only
tap), so its fair MuTap peer is the full `mutap` chain — not
`mutap-linear`.

## Direction 1 — their algorithms through our tests

`bench/compare/mutap_aec_compare` runs each subject through three
synthetic rooms (cabin / office / reflective, echo paths within the
canceller's modeling capacity), a speech-like far end and an independent
near end, at 48 and 16 kHz. Every metric is a **delay-invariant energy
ratio** (never a per-sample subtraction — a few ms of latency mismatch
between different black boxes would dominate that), and each subject's
declared latency aligns the timing measurements. Values are the median
across rooms.

Measured 2026-07 (`bench/compare/results/baseline.json`; shared host,
relative comparison is the meaningful part):

| Subject | fs | lat ms | ERLE dB ↑ | converge ms ↓ | reconverge ms ↓ | near-keep dB (0=ideal) | DT echo dB ↑ | DT near-keep dB (0=ideal) | x-realtime ↑ |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| **mutap** | 48000 | 10.7 | **62.2** | 80 | 3880 | +0.3 | **60.7** | +0.4 | 34 |
| **mutap** | 16000 | 32.0 | **87.2** | 280 | 1520 | +0.5 | **86.9** | +0.6 | 124 |
| mutap-f32 | 48000 | 10.7 | 62.2 | 80 | 3880 | +0.3 | 60.7 | +0.4 | 38 |
| mutap-f32 | 16000 | 32.0 | 87.3 | 280 | 1520 | +0.5 | 87.0 | +0.7 | 132 |
| mutap-linear | 48000 | 5.3 | 56.8 | 280 | — | −0.0 | 56.0 | +0.2 | 99 |
| mutap-linear | 16000 | 16.0 | 82.2 | 400 | — | −0.1 | 81.4 | +0.4 | 441 |
| speex | 48000 | 5.3 | 30.7 | 1160 | 2440 | −0.9 | 30.7 | −0.9 | 137 |
| speex | 16000 | 16.0 | 36.3 | 1360 | 3960 | −0.6 | 36.0 | −0.5 | 475 |
| webrtc | 48000 | 0.0 | 25.8 | **40** | **560** | −1.4 | 25.9 | −1.7 | 111 |
| webrtc | 16000 | 0.0 | 24.6 | **0** | **520** | −0.8 | 24.6 | −1.1 | 119 |

Column meanings: **ERLE** far-end single-talk echo depth (mic/out);
**converge/reconverge** time to 20 dB ERLE from cold and after a
mid-run echo-path swap; **near-keep** near-end-only transparency
(output/near level, 0 dB = untouched, negative = the canceller ducks
the near end); **DT echo** echo depth sustained through the double-talk
window; **DT near-keep** near-end level preserved during double talk.
"—" = never reached 20 dB within the 4 s tail.

Reading guide:

- **MuTap trades latency for depth, and wins depth by a lot.** 62 dB at
  48 kHz / 87 dB at 16 kHz is 2–3× AEC3's ERLE and ~2× Speex's, and its
  double-talk echo number barely drops from single talk (60.7 vs 62.2 at
  48 kHz) — the detector-free FD-Kalman robustness the ITU double-talk
  rows certify. The price is latency: two block hops (10.7 ms at 48 kHz,
  32 ms at 16 kHz) against AEC3's frame-aligned ~0.
- **AEC3 owns convergence and tracking.** It reaches 20 dB in one 40 ms
  window from cold and re-converges after a path swap in ~0.5 s, where
  MuTap's full chain takes 3.9 s to re-converge at 48 kHz (its rescue
  comparator is tuned conservative) and the raw linear core never makes
  20 dB inside the 4 s tail. If the echo path moves constantly, AEC3's
  agility matters more than steady-state depth.
- **Near-end preservation splits on philosophy.** MuTap and its linear
  core sit at ~0 dB near-keep in both near-end-only and double talk — the
  coherence-gated suppressor leaves the near end essentially untouched.
  Speex and AEC3 duck the near end 0.5–1.7 dB: acceptable, and in AEC3's
  case deliberate (its suppressor is tuned for perceived echo removal,
  not near-end transparency).
- **Cost.** MuTap's full chain is the most expensive (it does the most):
  ~34× real time at 48 kHz on a shared host vs AEC3's ~111× and Speex's
  ~137×. The raw MuTap linear core is competitive (99×), and at 16 kHz
  the linear core hits 441×. All four are comfortably real-time; this is
  a compute-per-quality axis, not a can-it-run axis.

## Direction 2 — our algorithm through their test (AECMOS)

`tools/compare/aecmos_eval.py` drives each subject through the tool's
`--wav` mode and scores the cleaned output with the AEC-Challenge AECMOS
predictor: an **echo MOS** (1–5, higher = less audible echo) and a
**degradation MOS** (1–5, higher = better near-end quality).

The committed run (`bench/compare/results/aecmos.json`) scores **real
speech** — distinct CMU ARCTIC speakers for the far and near ends —
through a **pyroomacoustics-simulated room** (image-source, RT60 ≈ 0.3 s,
6 dB ERL). AECMOS is trained on real speech, so these absolutes are
calibrated. The output is cross-correlation-aligned to the microphone per
subject so each canceller's processing latency is not a confound.

> The Microsoft AEC-Challenge *blind* clips would be the ideal input, but
> they are Git-LFS objects this environment's proxy will not authorize
> (`Not authorized to access repository microsoft/aec-challenge`).
> `--real-dir <dir of {st,nst,dt}_{far,mic}.wav>` points the identical
> driver at them when access is available; `--speech-dir` produces the
> real-speech-through-a-room set used here.

Measured with the 16 kHz AECMOS model (`Run_1663915512_Stage_0.onnx`),
echo MOS / degradation MOS (both 1–5, higher better):

| Subject | far-end ST (echo / deg) | double-talk (echo / deg) | near-end ST (echo / deg) |
|---|---:|---:|---:|
| mutap | **4.36** / 5.00 | 3.06 / 3.06 | 5.00 / 3.06 |
| speex | 1.64 / 5.00 | 3.39 / **4.15** | 5.00 / **4.00** |
| webrtc | **4.74** / 5.00 | **3.96** / 2.63 | 5.00 / 4.00 |

Reading guide — the calibrated real-speech picture is more balanced (and
more humbling for MuTap) than the short-path ITU battery:

- **Echo removal on a reverberant room favours the products with a
  residual stage.** WebRTC AEC3 (4.74) and MuTap (4.36) clear far-end
  echo well; **Speex collapses to 1.64** — its bare MDF canceller
  (64 ms filter) cannot model the ~300 ms reverb tail and has no
  suppressor to clean the residual, so echo stays audible. MuTap's
  coherence suppressor + comfort noise and AEC3's NLP mop it up.
- **Near-end quality is the mirror image.** Speex, which barely touches
  the near end, scores the best degradation MOS in double talk (4.15);
  **AEC3 scores the worst (2.63)** — its ~15 dB double-talk ducking
  (direction 1b) costs perceived near-end quality; MuTap sits between
  (3.06).
- **A perceptual metric does not reward raw ERLE 1:1, and long reverb
  narrows MuTap's lead.** MuTap's 87 dB ITU ERL was measured on a cabin
  path inside its 64 ms filter span; on a 0.3 s room its echo MOS (4.36)
  is close to AEC3's and its double-talk echo MOS (3.06) trails — the
  "echo tails beyond filter capacity" caveat, made audible. This is the
  single most useful thing running "our algorithm through their test"
  reveals.

## Building the harness

The comparison is off by default and never a CI gate. It needs the two
third-party cancellers built locally (both clone cleanly; only their
GitHub *release-asset* downloads are blocked behind restrictive proxies,
which the recipe below routes around):

```sh
# Speex: sources compiled directly, no autotools.
git clone https://github.com/xiph/speexdsp

# WebRTC AEC3: build webrtc-audio-processing (meson). Its abseil wrap
# pulls a GitHub release tarball; where that is blocked, build abseil
# from a git checkout and hand meson its pkg-config instead:
git clone --branch 20240722.0 https://github.com/abseil/abseil-cpp
cmake -S abseil-cpp -B absl-build -DABSL_ENABLE_INSTALL=ON \
      -DABSL_PROPAGATE_CXX_STD=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
      -DCMAKE_CXX_STANDARD=17 -DCMAKE_INSTALL_PREFIX=$PWD/absl-prefix
cmake --build absl-build && cmake --install absl-build

git clone https://gitlab.freedesktop.org/pulseaudio/webrtc-audio-processing
cd webrtc-audio-processing
PKG_CONFIG_PATH=$PWD/../absl-prefix/lib/pkgconfig \
  meson setup builddir --buildtype=release --default-library=static \
  --wrap-mode=nofallback -Dprefix=$PWD/../webrtc-prefix
meson install -C builddir
cd ..

# Configure MuTap's comparison harness against them:
cmake -S MuTap -B build -DMUTAP_BUILD_COMPARE=ON \
  -DMUTAP_COMPARE_SPEEX=ON  -DMUTAP_SPEEXDSP_DIR=$PWD/speexdsp \
  -DMUTAP_COMPARE_WEBRTC=ON \
  -DMUTAP_WEBRTC_PC_PATH="$PWD/webrtc-prefix/lib/x86_64-linux-gnu/pkgconfig:$PWD/absl-prefix/lib/pkgconfig"
cmake --build build --target mutap_aec_compare
```

With neither third-party option the tool still builds and compares the
three MuTap variants (linear vs chain, f32 vs f64) — useful on its own
for tracking those gaps over time.

Run it:

```sh
./build/bench/compare/mutap_aec_compare              # full matrix, text + JSON
./build/bench/compare/mutap_aec_compare --list       # linked-in subjects
./build/bench/compare/mutap_aec_compare --rate 16000 # one rate
./build/bench/compare/mutap_aec_compare --nl-sweep 16000 > bench/compare/results/nl_sweep.json  # distortion sweep

# AECMOS (direction 2). Model ships in microsoft/AEC-Challenge (git clone;
# the *.onnx models are committed, not LFS).
python3 tools/compare/aecmos_eval.py \
  --tool ./build/bench/compare/mutap_aec_compare \
  --model <AEC-Challenge>/AECMOS/AECMOS_local/Run_1663915512_Stage_0.onnx \
  --out bench/compare/results/aecmos.json
#   ...add --real-dir <dir of {st,nst,dt}_{far,mic}.wav> for the real corpus.
```

## Direction 1b — the ITU battery on all three cancellers

Beyond the core metrics above, every subject is also run through the
*actual* ITU compliance scenarios — the same P.501 signals, image-source
cabin/studio paths, dBm0 meters and masks the certified suite gates
(`tools/notebook/itu_compare_dump.cpp`, rendered by
[`notebooks/itu_comparison.ipynb`](../notebooks/itu_comparison.ipynb)).
This is the certified `test_itu_*` machinery pointed at black boxes.

Measured 2026-07 (`bench/compare/results/itu_comparison.json`), cabin
path, 48 / 16 kHz:

| Row | MuTap | WebRTC AEC3 | Speex MDF |
|---|---|---|---|
| Convergence to 40 dB ERL | fast, deepest floor | instant, shallow | slow, mid |
| Re-convergence after path swap | slow, deep | **fastest** | mid |
| Spectral atten. vs WB mask (§11.11.3) | **+15…40 dB over** | rides near / dips to it | comfortably over |
| DT echo loss, worst band (§11.12, ≥27 dB) | **37.8 / 38.0** | ~0 / 2.8 † | 38.9 / 53.5 |
| DT near-end ducking, worst band (≤3 dB) | **1.7 / 2.0** | 15.6 / 14.6 | **0.2 / 0.1** |
| Send activation build-up (≤50 ms) | 15.7 / 21.1 | 14.0 / 12.9 | 4.9 / 5.1 |

MuTap's DT echo loss reproduces the certified compliance figure
(37.5 / 38.0 dB) to a few tenths — the black-box path measures the same
chain the suite gates.

**† The AM-FM caveat.** The ITU double-talk echo-loss probe is an
orthogonal AM-FM tone comb. MuTap and Speex (adaptive filters) cancel it;
AEC3's speech-tuned nonlinear echo model does not engage on it, so its
~0 dB bar reflects signal mismatch, **not** its speech-echo capability.
The **near-end-ducking** column is the architecture-representative
double-talk result and needs no such caveat: AEC3 ducks the near end
~15 dB during double talk (echo-first by design), where MuTap and Speex
leave it essentially untouched.

## Direction 1c — nonlinear loudspeaker (where AEC3 earns its reputation)

Every path above is linear (room convolution). Real loudspeakers distort,
and a linear canceller can only model the echo component correlated with
the far end through a *linear* path — the harmonics a distorting speaker
adds survive as residual. A Hammerstein model (a memoryless scaled-
error-function nonlinearity, then the room) exercises exactly this, and it
is the single most important reframing in the comparison.

`mutap_aec_compare --nl-sweep` — **ERLE (linear cancellation depth) vs
loudspeaker THD**, 16 kHz (`bench/compare/results/nl_sweep.json`):

| THD % | MuTap | Speex | WebRTC AEC3 |
|---:|---:|---:|---:|
| 0.0 (linear) | **66.3** | 55.5 | 24.8 |
| 0.9 | 23.0 | 18.8 | 23.8 |
| 3.5 | 13.4 | 10.4 | **22.4** |
| 10.6 | 7.8 | 6.0 | **20.4** |
| 25.0 | 9.6 | 3.5 | **18.6** |

MuTap and Speex shed 40+ dB the instant the speaker distorts and **cross
AEC3 below ~1 % THD** (a decent speaker at moderate volume); AEC3's
nonlinear-aware suppressor holds ~19–24 dB across the whole range. The
linear-path dominance MuTap shows everywhere else is real *only on a
linear path*.

But the perceptual blow is softer, because MuTap is not only a linear
filter. `aecmos_eval.py --nl-sweep` — **AECMOS echo MOS vs THD** on real
speech (`nl_aecmos.json`) — barely moves for MuTap (4.36 → 4.26) or AEC3
(4.74 → 4.64): MuTap's residual suppressor + comfort noise mask most of
the residual its linear filter leaves, so raw ERLE *overstates* the
nonlinear vulnerability of a chain that carries a suppressor. Speex, with
no residual stage, stays low (floored by the reverb tail). The honest
summary: AEC3 wins nonlinear echo decisively on cancellation *depth*;
MuTap closes much of the *perceptual* gap with its suppressor; a bare
linear canceller (Speex) has no answer.

## What this comparison does not cover

Kept explicit, in the spirit of [itu-compliance.md](itu-compliance.md)'s
"listed, not claimed":

- **A memory / Volterra loudspeaker model.** Direction 1c uses a
  *memoryless* nonlinearity (SEF), the standard AEC test curve; real
  drivers add frequency-dependent (memory) distortion a full Volterra or
  power-filter model would capture. The memoryless case already exercises
  the linear-vs-nonlinear split; the memory refinement is a further step.
- **Echo tails beyond the filter's modeling capacity.** Paths are capped
  near the canceller's filter length so the comparison is of algorithm
  quality, not who was given more taps. Long reverberant tails (the ITU
  hall RIRs) are exercised by the compliance suite, not here.
- **Real-speech corpora and calibrated AECMOS.** Blocked by LFS access
  in this environment; the pipeline is real and one flag from the blind
  set.
- **The remaining ITU rows.** Direction 1b now runs the headline battery
  on all three (convergence, re-convergence, spectral mask, AM-FM comb
  double-talk echo loss + send attenuation, activation build-up). The
  longer tail — TCL, echo-level-vs-time, comfort-noise level/spectrum,
  noise pumping, the stability sweep — remains MuTap-only in the
  compliance suite; and the AM-FM echo-loss probe is adversarial to
  AEC3's speech tuning (see the † caveat above), so speech-signal echo
  loss is better read from the AECMOS table than that row.
- **AEC3 tuned configurations.** We run AEC3 with the echo canceller
  enabled and everything else off, for an apples-to-apples echo
  comparison. Its shipping product bundles AGC, noise suppression and a
  high-pass filter that change the delivered signal.
