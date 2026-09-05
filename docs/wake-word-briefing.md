# Wake-word detection — technical briefing

*Background research, 4 September 2026. No commitment; see
[`wake-word-plan.md`](wake-word-plan.md) for the implementation proposal that
builds on this.*

Formatted version: <https://claude.ai/code/artifact/2cc704b9-b4fd-41a1-9fd7-49f88071f285>

---

## 1. The constraints, not the classifier, are the problem

A wake-word detector is a small neural classifier run continuously over a
sliding window of audio features, emitting a probability every 10 ms, with
smoothing and a threshold on top. Everything interesting about the design comes
from the constraints; the classification itself is, by modern standards, easy.

Three numbers drive the architecture:

- **Always-on.** The front-end stage has a power budget in the low single-digit
  milliwatts and a memory budget in tens of kilobytes.
- **Streaming.** Nothing may look ahead more than a few tens of milliseconds. A
  model that needs the complete utterance before deciding is useless.
- **False-accept rate.** The usual product target is well under one false wake
  per day — roughly one false accept per 10–100 hours of arbitrary audio —
  while still catching 95 %+ of genuine utterances across accents, distances
  and noise.

That last requirement is the whole game. You are asking for something like a
1e-7 per-frame false-positive rate, and no single small model gets there. The
architecture everyone converges on is a consequence of that arithmetic.

## 2. The cascade

Products stage the power-versus-accuracy conflict. Each tier wakes the next and
each is orders of magnitude more expensive. No individual stage is precise
enough; the compounded rate is.

| Tier | Stage | Budget | Role |
|---|---|---|---|
| 1 | Acoustic VAD (analog / fixed-function) | microwatts | Rejects silence, which is most of the day |
| 2 | Keyword spotter (10k–250k params, int8) | ~1–3 mW, DSP or NPU | **"The wake-word algorithm."** Deliberately loose threshold — its only job is deciding whether to spend power |
| 3 | Verifier (+ speaker ID) | tens of mW, app processor | Re-scores the same audio with lookahead |
| 4 | Cloud ASR | network, watts | Final adjudication; can *retract* a wake already shown to the user |

A 1–2 s pre-roll buffer is re-read by every later tier — the device needs it
anyway so the command following the wake word is not clipped.

The tier-4 retraction path is why devices sometimes light up and then abort: the
cloud transcribed "hey, seriously?" rather than the wake word.

## 3. Features

Almost universally a log-mel filterbank: pre-emphasis, 20–30 ms frames at a
10 ms hop, windowed real FFT, power spectrum, 20–40 triangular mel bands, log
compression. MFCCs (a DCT on top) were standard in the HMM era; the DCT mainly
decorrelates for diagonal-covariance Gaussians and neural nets do not need it,
so log-mel is the modern default. Learned filterbanks (SincNet, LEAF) exist;
the gain is small and the cost real, so fixed mel remains the norm on-device.

The whole front end is a few thousand MACs per 10 ms frame — which is exactly
why it can live in a fixed-point DSP loop.

**PCEN** (per-channel energy normalization) replaces the plain log with adaptive
gain control plus compression: a one-pole smoother per mel channel, divided out,
then compressed. Markedly more robust to talker distance and channel variation
at essentially no cost; used in Google's on-device spotters.

### Three nested time scales

| Scale | Typical | What it sets |
|---|---|---|
| Analysis frame | 25 ms → 40 mel bands | Spectral resolution |
| Hop | 10 ms → 100 vectors/s | Decision rate |
| Model context | ~400 ms (30 frames left + 10 right) | How much of the phrase the classifier sees at once |
| Confidence window | ~1 s | How much evidence must agree before waking |

Streaming implementations cache intermediate activations so only the newest
10 ms of work is recomputed per hop, rather than re-running the whole 400 ms
context.

## 4. The model

**Keyword/filler HMMs** were the original approach: one HMM for the keyword's
phone sequence, one "filler" HMM for everything else, Viterbi decoding to
compare paths. Principled likelihood ratio, fiddly decoding, weak acoustic
model.

**Deep KWS** (Chen, Parada & Heigold, 2014) threw out the decoder. Stack a
window of frames — say 30 left and 10 right — feed a small fully-connected net,
output a posterior per keyword *word* plus a filler class. Smooth, threshold.
That is the entire system; it substantially beat the HMM approach and remains
the skeleton of everything since.

What changed since is only the topology, for MAC-count reasons:

- **CNNs** over the time–frequency patch (Sainath & Parada 2015).
- **Depthwise-separable CNNs (DS-CNN)** — winner of Arm's *Hello Edge*
  benchmark, still a strong default for microcontroller-class targets.
- **TDNNs / dilated temporal convolutions** — long receptive field cheaply, and
  they stream naturally. With activation caching you compute only the newest
  frame's work each hop. This is the single most important implementation trick
  for streaming efficiency, and what "streaming-aware training" in the TFLite
  Micro KWS tooling refers to.
- **CRNNs and GRU/LSTM stacks** — good receptive field, but recurrent state is
  awkward to quantize and the sequential dependency hurts on parallel hardware,
  so convolution-with-cache has largely won on-device.
- **Small transformers / conformers** at the verifier tier.

**End-to-end variants** skip frame-level labels: max-pooling loss over the
positive segment, CTC, or an RNN-T-style streaming decoder scoring the keyword's
label sequence. Practically important because frame alignments otherwise require
an ASR system you may not have.

## 5. The decision stage

Raw per-frame output is noisy; nobody thresholds it directly. The Chen et al.
formulation: smooth each class posterior over ~30 frames; form a confidence as
the geometric mean of the maximum smoothed posterior of each keyword word within
a ~100-frame window; fire when it crosses a threshold; apply a refractory period
so one utterance does not fire five times.

The threshold is the entire operating-point knob, chosen from a **DET curve**
plotting false-rejection rate against false-accepts-per-hour over hundreds or
thousands of hours of negatives.

> **Evaluation asymmetry.** Recall is measured *per utterance* on positives;
> false accepts are measured *per hour* on negatives. Any harness reporting a
> single accuracy figure over a balanced test set is measuring the wrong thing,
> and will make a model look finished that wakes on the radio nine times an
> evening.

Many shipping devices adapt the threshold at runtime — tightening in noise, or
after a recent false wake.

## 6. Training data, and why negatives are the work

Positives are comparatively cheap: a few thousand to a few hundred thousand
recordings, multiplied by augmentation — additive noise at varied SNRs, RIR
convolution for reverberation and distance, speed/tempo perturbation,
SpecAugment-style masking, simulated codec and microphone response. Synthesized
positives from multi-speaker TTS work surprisingly well and are what make
arbitrary custom wake words feasible at all.

Negatives are where the effort goes. Random audio is easy to reject and teaches
little. What matters is **hard negative mining** — phonetically confusable
material. For "Hey Jarvis" you need thousands of "hey Travis", "hey, Jarvis
said", "a garage is".

Product teams choose wake words partly for this reason: three or four syllables,
unusual phonotactics, distinctive stress, preferably not a substring of common
speech. "Alexa" and "Siri" are short and pay for it; "OK Google" and "Hey
Snapdragon" are long and confusable-poor by design.

The other perpetual source of negatives is device-side false accepts returned
from the field — simultaneously the most valuable training data available and
the reason wake-word telemetry is a privacy flashpoint.

## 7. Custom and few-shot wake words

**Query-by-example / embedding** systems train a speech encoder to map audio to
a fixed embedding, enrol from three repetitions, detect by cosine distance. No
retraining, arbitrary phrases, noticeably worse than a trained model.

**Phoneme-posterior** systems run a small generic phone recognizer and match the
target phrase's phone sequence against the posteriorgram, so a wake word can be
specified as text alone. Commercial engines (Picovoice Porcupine among them) do
a variant of this, compiling a phrase into a small model offline.

## 8. On-device numerics

The shipping form is almost always **int8 weights and activations** with
per-channel scales, from post-training quantization with a calibration set or
from quantization-aware training. Accuracy cost typically well under a point.

The feature front end runs in Q15 or Q31 on M4/M33/M55-class parts, with the FFT
and mel accumulation carrying the usual headroom-versus-precision negotiation,
and MVE/Helium giving the dot products a 4–8× lift. On a Cortex-M55 a DS-CNN
spotter is comfortably real-time at a few percent duty cycle; on an M4 it is
tighter but done routinely.

Total footprint for a credible tier-2 model: ~20–100 KB of weights plus the
pre-roll ring buffer.

## 9. Open corpora

The field is unusually well served by open data, with one catch: the free
corpora give you *keywords*, *negatives* and *noise*, but almost nobody
publishes a real product wake word under a commercially usable licence.

### Keyword-specific

| Corpus | Scale | Licence | Good for |
|---|---|---|---|
| Speech Commands v2 | 105,829 one-second clips, 35 words, ~2,600 speakers, 16 kHz | CC BY 4.0 | The canonical benchmark. Contains `marvin` and `sheila` — name-shaped by design — plus a background-noise folder. Build the harness against this first. |
| Multilingual Spoken Words (MSWC) | ~23.4 M clips, 340k keywords, 50 languages | CC BY 4.0 | The big one. Force-aligned out of Common Voice. The only large keyword set unambiguously commercially usable, and the natural source of phonetically confusable hard negatives. |
| MobvoiHotwords | ~175k utterances, 2 Chinese wake phrases | OpenSLR SLR87 | Genuinely wake-word-shaped: multi-syllable, far-field, varied SNR, real negatives — which Speech Commands is not. |
| HI-MIA / HI-MIA-CW | Mandarin, far-field multi-mic | OpenSLR SLR85 | Joint wake-word + speaker verification. |
| Hey Snips | ~11k positives, ~2,200 speakers, ~86k negatives | research-use | Closest public thing to a real product set. **Availability unreliable since the Sonos acquisition — verify terms and mirrors before planning around it.** |
| Qualcomm Keyword Speech | 4 keywords, 50 speakers, ~4,000 utterances | non-commercial | Real product-style phrases, research licence only. |

### Negatives — which set the DET curve

| Corpus | Scale | Licence | Character |
|---|---|---|---|
| Common Voice | thousands of hours, many languages | CC0 | Cleanest licence in speech, huge speaker diversity. The default negative pool. |
| LibriSpeech / Libri-Light | ~1k h labelled / ~60k h unlabelled | CC BY 4.0 | Read speech — tonally unlike ambient audio, but cheap volume. |
| People's Speech | ~30k h | CC-BY / CC-BY-SA | One of the two largest permissive sets. |
| VoxPopuli | ~400k h unlabelled | CC0 | European Parliament recordings; enormous and unencumbered. |
| AMI Meeting Corpus | ~100 h | CC BY 4.0 | Spontaneous multi-party speech with overlap and crosstalk — much closer to what a device actually rejects all day. |

### Noise and impulse responses

| Corpus | Content | Licence | Role |
|---|---|---|---|
| MUSAN | ~109 h music / speech / noise | CC BY 4.0 | Standard additive-noise set in Kaldi-lineage recipes. |
| OpenSLR SLR28 | simulated + real RIRs | permissive | Standard reverberation augmentation; gives you distance and room. |
| FSD50K | labelled environmental sound | CC BY | Targeted hard negatives: television, kitchen clatter, dogs. |
| DEMAND | 16-channel real noise, 18 scenes | CC BY-SA | Realistic scene noise where a synthetic mix would be too clean. |
| WHAM! / UrbanSound8K | noise | CC BY-NC | Useful, but non-commercial. |

## 10. The working recipe

No open corpus contains *your* wake word, so the current standard approach —
and it works well — is to skip the recording entirely:

1. **Synthesize positives with TTS.** Piper (MIT) plus `piper-sample-generator`
   produces tens of thousands of variants of an arbitrary phrase across voices,
   rates and pitches. This is exactly what openWakeWord and microWakeWord
   (ESPHome) do; both ship their pipelines permissively.
2. **Augment hard.** RIR convolution from SLR28, additive noise from MUSAN and
   FSD50K across 0–20 dB SNR, gain and mild speed perturbation, simulated
   microphone response.
3. **Mine hard negatives from MSWC** — every keyword within small phonetic edit
   distance of the target phrase.
4. **Bulk negatives from Common Voice, AMI and People's Speech**, ideally
   hundreds of hours, because the false-accept target is measured per hour.
5. **Hold out real recorded positives** — even a couple of hundred utterances
   from real speakers at real distances — as the *evaluation* set.

> **The failure mode to design against.** Synthetic training with synthetic
> evaluation will lie to you cheerfully. TTS positives are too clean and too
> centred in prosody space; a model can score beautifully against them and fall
> apart on a real talker three metres from the microphone. The recorded hold-out
> set is not optional polish — it is the only thing standing between a promising
> number and a working detector.

## 11. Licence traps

Fine for a paper, unusable in a product: **TED-LIUM 3** is CC BY-NC-**ND**;
**VoxCeleb** is non-commercial and has had takedown friction; **GigaSpeech**'s
subsets carry mixed terms; **WHAM!** and **UrbanSound8K** are non-commercial.
**AudioSet** distributes labels, not audio — the recordings are YouTube
references and are not redistributable.

Two further checks: both openWakeWord's and microWakeWord's *models* are trained
on synthetic data whose upstream TTS voice licences are separate from the code
licence and must be verified independently; and terms on these sets do change —
a licence confirmed at project start is not a licence confirmed at ship.

---

### Confidence notes

- Figures are as published by each project and were not re-counted here.
- Licence characterizations are a starting point for diligence, not a legal
  opinion — verify each corpus at the point of use.
- Two items carry known uncertainty and are flagged in place: Hey Snips
  availability, and the upstream voice licences behind synthetic-positive
  pipelines.
