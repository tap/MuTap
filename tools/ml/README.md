# tools/ml — machine learning for echo cancellation, without IP entanglement

This directory answers a question: **is there an ML approach to echo
cancellation MuTap could adopt that is not encumbered by IP licensing?**
It contains the benchmark that measured the answer, the clean-licensed
training pipeline the answer recommends, and the training/inference code
for a learned residual suppressor that slots into the existing
post-filter stage.

Nothing here builds into the library or the emulated-target CI; it is
host-side tooling (`-DMUTAP_BUILD_ML_TOOLS=ON` + Python).

## The shape of the answer

End-to-end neural echo cancellation replaces the whole chain with a DNN.
Measured below, it is not what MuTap wants: it cannot run on the M55
target, it cannot be certified against the ITU battery's transparency
clauses, and — measured, not speculated — it silently destroys any
near-end material it was not trained on. The approach that fits is the
**hybrid** the field converged on (RNNoise lineage; the linear-AEC +
neural-post-filter systems throughout the ICASSP AEC Challenges): keep
MuTap's linear canceller exactly as it is, and learn only the
residual-echo post-filter — the stage whose classical implementation is
already a heuristic (coherence gate + learned leakage), and the only
stage where a data-driven decision rule plausibly beats DSP.

## Licensing map (the point of the exercise)

| Component | License | Role here | Shippable? |
|---|---|---|---|
| DTLN-aec code + weights ([breizhn/DTLN-aec](https://github.com/breizhn/DTLN-aec)) | MIT | benchmark baseline | code yes; **weights benchmark-only** — trained on the mixed-license AEC-Challenge corpus (parts academic-only) |
| RNNoise architecture ideas ([xiph/rnnoise](https://github.com/xiph/rnnoise)) | BSD-3 | design reference (band gains, tiny GRU) | yes (we reimplement, share no code) |
| NKF-AEC ([fjiang9/NKF-AEC](https://github.com/fjiang9/NKF-AEC)) | **no license file** | paper-only inspiration | repo code unusable; clean-room from the ICASSP 2023 paper only |
| WebRTC AEC3 ([webrtc-audio-processing](https://gitlab.freedesktop.org/pulseaudio/webrtc-audio-processing) >= 1.0) | BSD-3 | benchmark baseline (Chrome's deployed canceller) | yes |
| LibriSpeech / Mini LibriSpeech ([openslr.org](https://www.openslr.org/31/)) | CC BY 4.0 | training speech | yes, with attribution |
| Rooms, echo paths, mixtures | synthesized here | training scenarios | yes (ours) |
| Trained suppressor weights | produced by this pipeline | the deliverable | yes — every input is MIT / CC BY / ours |
| PyTorch (training only) | BSD-3 | trainer | never shipped; inference is dependency-free |

Patent posture: the hybrid structure (linear AEC + learned spectral
post-filter) is published academic work from 2018–2023 with permissively
licensed implementations — a favorable prior-art landscape, and the
inference we'd ship is ~50k parameters of GRU arithmetic implemented
from scratch. Not a legal opinion; a commercial embedded product still
warrants a freedom-to-operate review.

## What was measured

Protocol: the AEC test rig's double-talk protocol (`tests/test_aec.cpp`)
plus a near-end-only transparency segment, all systems on identical
signals, one shared delay-compensated meter (`metrics.py`). Signals read
as 16 kHz. Metrics: single-talk ERLE (converged) / double-talk true-echo
suppression / near-end preservation SDR, all dB, medians.

**Rig materials (synthetic — speech-envelope AR, voiced, music), studio
room, seeds {2,12,22}:**

| system | stERLE | dtSUP | neSDR |
|---|---|---|---|
| mutap-kalman-linear | 18.6 | 13.4–15.2 | 80 (transparent) |
| mutap-chain (classical post-filter) | 42–46 | 12.2–15.9 | 29–40 |
| dtln-aec-128 | 36.3 | **≈ 0** | **0–5** |
| dtln-aec-512 | 37–44 | **≈ 0** | **0–4** |

DTLN-aec removes echo well while *only* echo is present, then — on
material outside its training distribution — cancels the *near end*
too: an AR-noise or music "talker" comes back at ≈0 dB SDR (annihilated).
The classical engines are material-agnostic by construction. This is the
core risk of end-to-end neural AEC for a tool like MuTap, whose users
point it at arbitrary program material.

**Speech scenarios (LibriSpeech, in-domain for DTLN; random rooms,
3 scenarios):**

| system | stERLE | dtSUP | neSDR |
|---|---|---|---|
| mutap-kalman-linear | 8.0 | 10.9 | 80 (transparent) |
| mutap-chain | 30.9 | **17.2** | **38.2** |
| dtln-aec-128 | 43.2 | 12.2 | 24.2 |
| dtln-aec-512 | **43.4** | 14.2 | 29.3 |

On its home turf DTLN is credible — and the chain still wins double-talk
suppression and near-end fidelity. DTLN's single-talk ERLE lead
(~+12 dB median) is the gap a *learned residual suppressor* should
close while keeping the classical chain's guarantees. That is the
hybrid this directory trains.

## The trained hybrid (v1)

`pretrained/suppressor_v1.munn`: 51,670 parameters, 12 epochs (~2.5 h
CPU) on 400 examples (~1.1 h of mixture) from Mini LibriSpeech
train-clean-5 — deliberately modest, to measure what the architecture
buys before scaling anything. Same meter, speech scenarios:

| system | stERLE | dtSUP | recERLE | neSDR |
|---|---|---|---|---|
| mutap-kalman-linear | 8.0 | 10.9 | 9.6 | 80 |
| mutap-chain | 30.9 | **17.2** | 34.0 | **38.2** |
| mutap-kalman+nn (v1) | **36.0** | 11.1 | **36.4** | 37.6 |
| dtln-aec-512 | 43.4 | 14.2 | 26.3 | 29.3 |
| webrtc-aec3 | 37.9 | 2.7 | 39.2 | **4.5** |

WebRTC AEC3 (Chrome's deployed canceller, run through the same meter via
`webrtc_aec3_infer`) is the cautionary classical datapoint: excellent
ERLE, and the worst near-end transparency of the field by a wide margin —
its aggressive suppressor clamps the talker (4.5 dB SDR). That is a
*design choice* for telephony (callers tolerate ducking; they complain
about echo), and the opposite of the right choice for program material.

And on the rig's synthetic materials (out of training domain — the
DTLN-killer test), medians over materials {ar, voiced, music} x seeds:

| system | stERLE | dtSUP | neSDR |
|---|---|---|---|
| mutap-chain | 42–46 | 12.2–15.9 | 29–40 |
| mutap-kalman+nn (v1) | 42.9–43.4 | **≈ 0** | **26–39** |
| dtln-aec-512 | 37–44 | ≈ 0 | 0–4 |

Reading:

- **In domain, the hybrid already beats the classical chain on ERLE**
  (+5 dB single-talk, +2 dB recovery) at equal near-end transparency,
  from a first, small training run.
- **Out of domain it degrades gracefully where DTLN catastrophically
  fails**: the near end survives at 26–39 dB SDR (vs DTLN's 0–5 —
  annihilation), because the linear canceller does the bulk, the network
  only gains E within [0, 1], and Yhat-referenced features carry real
  echo evidence. But its *double-talk suppression* contribution
  evaporates off-domain (≈0 dB vs the coherence rule's 12–16), so the
  classical suppressor remains the right default engine.
- Next steps, in expected-value order: (1) add the coherence statistic
  to the feature set — it is exactly the evidence the classical rule
  thrives on and the v1 net lacks; (2) train on synthetic/music near
  ends too — unlike DTLN we own the generator, so off-domain robustness
  is a data problem we can actually fix; (3) more data/epochs (v1's val
  loss was still falling); (4) int8 quantization + CMSIS-NN for the M55
  path.

## The pipeline

```
                 (LibriSpeech CC BY 4.0)
make_dataset.py ──► mixtures: room, delay, saturation, SER, double-talk
                 ──► linear Kalman canceller (C ABI — the REAL residual)
                 ──► features.py: 22 ERB band energies of E and Yhat
                 ──► shards: (features, oracle gains, loss weights)
train_suppressor.py ──► dense64 → GRU96 → dense22 (~51k params) ──► .npz
nn.py            ──► numpy reference inference (+ benchmark system)
run_benchmark.py ──► same meter, all systems, incl. --nn-weights hybrid
```

Reproduce:

```sh
# 1. benchmark baselines (DTLN models: clone breizhn/DTLN-aec, MIT)
python3 tools/ml/run_benchmark.py --dtln-dir <dtln>/pretrained_models

# 2. data (Mini LibriSpeech: openslr.org/31, CC BY 4.0)
python3 tools/ml/make_dataset.py --corpus <LibriSpeech>/train-clean-5 \
    --out shards --examples 400 --seed 1

# 3. train (CPU, minutes)
python3 tools/ml/train_suppressor.py --data shards --out suppressor.npz

# 4. evaluate the hybrid, same meter
python3 tools/ml/make_dataset.py --corpus <LibriSpeech>/dev-clean-2 \
    --scenarios scen --examples 3 --seed 7
python3 tools/ml/run_benchmark.py --scenario-dir scen --nn-weights suppressor.npz
```

WebRTC AEC3 baseline (optional): build freedesktop's
`webrtc-audio-processing` (>= 1.0 carries AEC3; distro 0.3.x packages are
the legacy canceller) and point pkg-config at it before configuring:

```sh
git clone --depth 1 --branch v2.1 \
    https://gitlab.freedesktop.org/pulseaudio/webrtc-audio-processing.git
meson setup webrtc-audio-processing/build webrtc-audio-processing \
    --buildtype=release -Dprefix=$PWD/wap-install
ninja -C webrtc-audio-processing/build install
PKG_CONFIG_PATH=$PWD/wap-install/lib/*/pkgconfig cmake -B build-ml ... # as above
python3 tools/ml/run_benchmark.py --webrtc-aec3-bin build-ml/tools/ml/webrtc_aec3_infer ...
```

`features.py` is the single source of truth for the analysis geometry
(128/64 sqrt-Hann STFT, 22 ERB bands, feature normalization, oracle
gain/weight definitions). The C++ inference of the trained suppressor
must match `nn.py` to float precision; keep them in lockstep.
