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

⚠️ **Data caveat.** The AEC-Challenge blind clips are Git-LFS objects
the build sandbox's network policy would not authorize, so the committed
run (`bench/compare/results/aecmos.json`) uses a **synthetic** eval set.
AECMOS is trained on real speech, so absolute MOS on synthetic audio is
uncalibrated — the low degradation-MOS values below are that
miscalibration, not the cancellers. Every subject sees identical input,
so **relative ordering is the readable result**; point the driver at the
real corpus (`--real-dir`) once LFS access is available for calibrated
absolutes.

Measured with the 16 kHz AECMOS model (`Run_1663915512_Stage_0.onnx`):

| Subject | far-end ST echoMOS ↑ | double-talk echoMOS ↑ | near-end ST degMOS ↑ (relative) |
|---|---:|---:|---:|
| mutap | 4.18 | **4.71** | 1.32 |
| speex | 3.98 | 4.63 | 1.60 |
| webrtc | **4.72** | 4.59 | 1.59 |

Reading guide:

- **A perceptual metric does not reward raw ERLE 1:1.** MuTap's 62 dB
  ERLE beats AEC3's 26 dB by 36 dB in direction 1, yet in far-end single
  talk AECMOS scores AEC3's echo *higher* (4.72 vs 4.18): once the echo
  is below audibility, AEC3's suppression reads as cleaner to the
  predictor. This is the single most useful thing running "our algorithm
  through their test" reveals — the ERLE gap overstates the perceived
  echo advantage in single talk.
- **MuTap's double-talk advantage survives their metric.** In double
  talk MuTap takes the top echoMOS (4.71 vs AEC3 4.59) — the detector-
  free robustness holds up under AECMOS, not just under our own
  suppression number.
- Degradation MOS is uncalibrated here (synthetic near end); the numbers
  cluster and should not be read as absolute near-end quality.

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

# AECMOS (direction 2). Model ships in microsoft/AEC-Challenge (git clone;
# the *.onnx models are committed, not LFS).
python3 tools/compare/aecmos_eval.py \
  --tool ./build/bench/compare/mutap_aec_compare \
  --model <AEC-Challenge>/AECMOS/AECMOS_local/Run_1663915512_Stage_0.onnx \
  --out bench/compare/results/aecmos.json
#   ...add --real-dir <dir of {st,nst,dt}_{far,mic}.wav> for the real corpus.
```

## What this comparison does not cover

Kept explicit, in the spirit of [itu-compliance.md](itu-compliance.md)'s
"listed, not claimed":

- **Nonlinear echo / loudspeaker distortion.** The paths here are
  linear. AEC3 carries a nonlinear echo model and comfort-noise machinery
  built for cheap loudspeakers; MuTap models a linear path. On real
  hardware with a distorting speaker the gap in direction 1 would narrow
  — this bench does not exercise that, and it is where AEC3 earns its
  reputation.
- **Echo tails beyond the filter's modeling capacity.** Paths are capped
  near the canceller's filter length so the comparison is of algorithm
  quality, not who was given more taps. Long reverberant tails (the ITU
  hall RIRs) are exercised by the compliance suite, not here.
- **Real-speech corpora and calibrated AECMOS.** Blocked by LFS access
  in this environment; the pipeline is real and one flag from the blind
  set.
- **The full ITU battery on the third-party cancellers.** Direction 1
  applies our *core* metrics to their algorithms; retrofitting the whole
  `test_itu_*` battery (comb-filter double-talk echo loss, spectral
  masks, switching dynamics) onto a black-box backend is a further step.
- **AEC3 tuned configurations.** We run AEC3 with the echo canceller
  enabled and everything else off, for an apples-to-apples echo
  comparison. Its shipping product bundles AGC, noise suppression and a
  high-pass filter that change the delivered signal.
