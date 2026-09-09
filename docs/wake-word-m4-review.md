# Review of the `mutap.wake~` plan, milestone M4

*Review, 8 September 2026, of [`wake-word-plan.md`](wake-word-plan.md) rev 2,
§6 "M4 — Corpus, splits and dataset builder" (lines 471–513), read at
`96b4572` with M0–M3 done and the DspTap pin at `58210ae`. Nothing in the
plan was changed by this review; every amendment below is a recommendation
for a rev 3 of the M4 section. Line numbers are rev 2's.*

Formatted version: <https://claude.ai/code/artifact/f4c43586-6e08-48fb-b214-6c7e52df43e8>

---

## 1. Verdict

**M4's skeleton is right and should be kept. As written it cannot be built
without the builder deciding roughly a dozen things the plan is silent on,
and one of them is irreversible.**

What is right: speaker-disjoint splits keyed across MSWC and Common Voice; one
named, never-trained-on eval negative set with a music share as the FA/hour
denominator for every number in the plan; a committed manifest with an
out-of-git feature store; a dataset card; a consented, recorded hold-out set
measured on the microphone paths that ship. The audit's issue-3, -5 and -11
amendments are genuinely present in the text.

What is not: the hold-out is human voice data going into a public MIT
repository, and M4 says nothing about what the talkers consent to, under which
licence the audio is released, in what form it is stored, or how large it may
be — while the one precedent it cites (`make_rir_fixtures.py`) stamps
`SPDX-License-Identifier: MIT` on every fixture it emits. That decision cannot
be undone once audio enters git history, and the M4 pass cannot fail on it. Two
further prerequisites are missing rather than wrong: the numpy reference
`kws_features.py` is told to import is hard-wired to one geometry, so "owns
the values", "imports rather than restates" and "retraining never touches
DspTap" cannot all hold without a DspTap change or a stated geometry freeze;
and the rebuild pass names nothing a rebuild is compared against, so it is a
smoke check. Two external facts have moved under the plan since rev 2: the
maintained Piper is GPL-3.0 (espeak-ng embedded), and Common Voice is now
account-gated with a no-re-hosting clause on top of CC0.

Before any recording or feature build: settle consent scope, audio licence and
storage form; decide whether DspTap's reference is parameterized (a submodule
PR and pin bump) or M4–M6 train at the reference geometry; give the rebuild
pass a comparison target and a committed toy fixture; and move the
Pico-microphone path of the hold-out to M7's loudspeaker replay. Stage M4 as
M4a (builder, contracts, Speech Commands, CI job) → M4b (full corpus, phrase,
splits, lock, card) → M4c (hold-out protocol and host-path recording), with the
≥ 10-talker / ≥ 200-utterance target enforced at M6's pass, not M4's (§7).

## 2. How the review was run

The same shape as the [rev-1 audit](wake-word-audit.md): every claim checked
against the working checkouts of MuTap and DspTap rather than against the
plan's description of them. MuTap-Max, RatioTap and TapTools were not on disk;
claims about them are marked unverifiable, never asserted.

| Phase | Agents | What they did |
|---|---|---|
| Attack | 8 + 1 | One adversarial reviewer per lens — corpora and splits; the feature pipeline and its contract; licensing, provenance and consent; reproducibility and the builder; sequencing and dependencies; labels, endpoints and augmentation; compute and storage; evaluation statistics — plus an external-facts checker with live web access (28 facts, each with its source URL). |
| Verify | 84 | One skeptic per finding, instructed to refute it against the files and to default to refuted. |
| Critique | 1 + 9 | A completeness critic for what no lens examined; its findings verified the same way. |
| Judge | 1 | A cross-lens judge merging findings into root issues, ranking them, naming findings the skeptics should not have let through, and drafting the amendments and staging. |

93 findings were raised. 1 was confirmed outright, 92 partially confirmed (a
real point, overstated or mis-cited, with the accurate statement recorded), 0
refuted. As in the rev-1 audit, a zero refutation rate means the skeptics leaned
lenient, so the judge's list of fifteen discounted findings (§5) is carried as
the correction. The main session then spot-checked the top-ranked root issue's
evidence by hand and re-verified the two external facts with the largest
consequences on the live pages (§Provenance).

## 3. Root issues, ranked by how much they change what happens before M4 is built

### 1. The hold-out is human voice data in a public MIT repository, and M4 decides none of consent scope, release licence, pseudonymity, storage form or size — *critical*

**What the plan says.** :497–502: "N talkers (target ≥ 10) … owner, consent
and permitted-use row per talker, target ≥ 200 utterances … Committed as a
fixture with provenance, as the RIR fixtures are." :513, the pass: "hold-out
fixture committed with its consent rows." :124, the licence column: "ours;
consent and permitted use recorded per talker". :494–495: "the repo carries
manifests and the builder only."

**What the review found.** Nothing in the plan, audit, briefing or HANDOFF says
what the consent must cover — public redistribution, permanence in git history
and forks, the loudspeaker replay M7 performs on the recordings (:587–591) —
names a licence for the audio ("ours" is ownership, not a licence), requires
pseudonymous talker ids, or says where signed forms live. `LICENSE` grants MIT
over all repository content with no data carve-out, and the cited precedent
emits `// SPDX-License-Identifier: MIT` / `// Copyright 2026 MuTap
contributors` on every fixture (`tools/fixtures/make_rir_fixtures.py:108–109`;
`tests/fixtures/rir_cabin.h:2–3`), so a hold-out built "as the RIR fixtures
are" asserts MIT and MuTap copyright over a person's voice by default. The pass
is satisfied by a consent row reading "evaluation use" while the commit
performs redistribution: it cannot fail on the defect. The audit asked for a
"consent/licence row" (audit :175, :232); rev 2 carried the consent half and
dropped the licence.

Form and size are also unstated. The precedent is a 4096-float C header
(81,317 B for 4,096 taps ≈ 20 B/sample) consumed by C++ tests; the hold-out's
consumers are the M5 Python harness through the C ABI (:526) and the M7 bench
(:587–591). 200 utterances × 2 s × 16 kHz × 2 B × 2 paths ≈ 25.6 MB as int16
(≈ 250 MB as header text) in a repository whose whole pack is 8.3 MiB, whose
largest tracked file is 1.5 MB, with no LFS, no `.gitattributes`, no tracked
audio, and whose only prior real-audio set is deliberately git-ignored
(`.gitignore:13–15`, `tests/data/itu/`). :494–495 and :502 are unreconciled.

Dropped from the findings as overstated: that the plan "chose C headers" (the
clause modifies provenance, as the audit read it at :176–177); that CI
artifacts are involved (§7 :709 keeps the DET out of CI); and that "the M6 pass
is inside the noise" (the committed count is deterministic; the ±3 % interval
bears on generalization).

**What to change.** Add a paragraph to the hold-out specification, before the
first take: (1) the audio is released under a named licence (CC BY 4.0 with
pseudonymous credit, or CC0), never MIT by default, with a carve-out line in
`LICENSE` / `THIRD_PARTY_NOTICES.md`; (2) a committed consent template whose
scope names public redistribution under that licence, permanence (removal from
the tip only), ML evaluation including loudspeaker replay and both microphone
paths, and a withdrawal policy; signed forms off-repo, referenced by id;
(3) talkers appear only as pseudonymous ids; (4) `DATASET.md` carries a
repository privacy statement distinct from §5's runtime one; (5) the fixture
form — 16 kHz FLAC/WAV under a named path with a committed manifest of per-file
sha256, talker id, condition, path and endpoint, or audio in the store with only
the checksummed manifest in git — and a size ceiling; (6) "as the RIR fixtures
are" → "with the RIR fixtures' provenance rule"; (7) the pass becomes "the
builder refuses any utterance whose consent id is missing or whose scope lacks
public redistribution" — `make_rir_fixtures.py:216–217`'s refusal pattern.

### 2. The numpy reference M4 must import is hard-wired to one geometry — *major*

**What the plan says.** :473–474: "`kws_features.py` as source of truth for the
feature *values* (the M1 numpy reference, now the real thing)"; :323–324:
"M4's `kws_features.py` imports it through the submodule rather than restating
it"; :147–150: every trainer-tunable parameter "is *runtime geometry* …
Retraining never touches DspTap."

**What the review found.**
`submodules/dsptap/tools/reference/make_frontend_reference.py` binds
`SR/FRAME/HOP/FFT/BANDS/FMIN/FMAX` (:58–64), `LOG_*` (:65–67) and `PCEN_*`
(:68–72) as module globals; `mel_energies` (:111–127), `log_features`
(:130–131) and `pcen_features` (:134–140) read them; only `mel_weights` (:88)
is parameterized, and there is no sqrt-Hann path although `log_mel.h` offers
one. Importing it yields features at the reference geometry only; any tuned
value requires assigning the submodule's globals, editing DspTap (contradicting
:150), or restating the formulas in MuTap (breaking the one-copy rule the
script's own docstring states, :5–10). The pattern M4 is told to copy already
has the right shape — `tools/ml/features.py` threads a frozen `Geometry`
dataclass through everything — and the engine side is fully parameterized
(`dsptap_py.LogMel` :208–225; the C ABI :73–87); the reference is the odd one
out. DspTap's C++-vs-numpy parity runs only at `log_mel_geometry{}`
(`tests/test_log_mel.cpp:104–126`), and the M1 record's "bridge reproduces the
numpy reference to 1.5e-14" has no committed caller.

Nothing blocks M4 at the reference geometry — `log_mel.h`'s defaults equal the
reference's constants — so this blocks the first *tuned* parameter, which §5
and the audit's issue 8 say is exactly what a KWS pipeline iterates on. The
per-frame Python loop is the family's existing pattern and a one-off cost
(108 M frames for 300 h; 0.6–2.4 h at an unmeasured 20–80 µs/frame), not a
defect.

**What to change.** Decide in M4's first paragraph and list as a prerequisite:
(a) a small DspTap PR — a geometry dataclass mirroring `log_mel_geometry` field
for field, window and PCEN included, threaded through the three functions,
defaults preserved so `frontend_vectors.h` regenerates byte-identical and M1's
pins do not move — then the MuTap pin bump; `kws_features.py` becomes the
manifest's geometry values + that import + a self-check against
`dsptap_py.LogMel` at the manifest geometry on M1's reference signal, pinned at
a measured tolerance; or (b) state that M4–M6 train at the reference geometry
(plain-log default) and file the parameterization as a follow-up, deleting "now
the real thing". Under either: features are computed in double (numpy or the
double bridge — identical to ~1e-14) and stored float32, and `kws_features.py`
asserts band support at the chosen geometry, since `log_mel.h:286–289`
silently zero-fills a band with no FFT bin inside it.

### 3. The rebuild pass names no comparison target, the manifest carries the wrong contract version, and the builder has no regression fixture or CI job until M6/M8 — *major*

**What the plan says.** :511–512: "dataset rebuilds from the manifest on a
clean checkout with the feature store mounted"; :492–493, the schema: "corpus
release ids, archive checksums, decoder versions, `kws_features.py` contract
version, split assignment, augmentation seeds"; :225–227: the front-end
contract version is "the `log_mel` formula-contract version the model was
trained against"; :656: the toy-corpus CI run is "draft M6, final M8"; :246–247:
each stage has "a pass criterion that *can fail* and a committed regression
fixture."

**What the review found.** No line defines what a rebuilt shard must equal;
the only checksums are for input archives, so a store built from an earlier
manifest revision, a swapped decoder or resampler, or a drifted Piper build
passes while the split script verifies a manifest the shards were not built
from. The audit (:190) asked the manifest to carry the "front-end contract
version"; rev 2 substituted a MuTap-owned version, and the reference has no
version constant — so under runtime-first (:227–229) a store built at
`k_contract_version` 1 and exported after a DspTap bump gets stamped with
whatever `LogMel.contract_version()` returns *then*: the silent degradation §5's
load check exists to catch. The resampler is unnamed although the sources are
not uniformly 16 kHz (Common Voice 48 kHz MP3, MSWC opus at 48 kHz, all four
Piper voices 22,050 Hz — verified; FMA 44.1 kHz); the copied precedent's
`resample()` (`make_dataset.py:67–76`) is an upsampling brick wall, and the RIR
generator uses `scipy.signal.resample_poly` without recording window or
version. The suppressor's own pattern writes geometry into every shard and
refuses a mix (`make_dataset.py:304`; `train_suppressor.py:51`); M4 does not
say to carry it forward. The committed fixture §6 requires is absent for the
builder (the hold-out is evaluation audio), and the only CI exercise of the
builder is the training-guide toy run two milestones later. Bit-identical
cross-platform shards are not the right bar (HANDOFF working note 2; the M2
Nyquist defect was a 2e-11 libm difference) — exactness on the pinned Mac plus
a measured tolerance elsewhere is, and the plan states neither.

**What to change.** Replace the first pass item with: (1) the builder emits a
**lock** beside the store — per clip: decoded-PCM sha256, length, split, label,
endpoint, resolved augmentation parameters; per shard: sha256 and frame count;
per split × class: hours and counts; toolchain: python/numpy/soundfile/
resampler/piper/espeak-ng versions, the DspTap submodule commit and
`dsptap_log_mel_contract_version()`; (2) every shard header carries
`k_contract_version`, the DspTap commit, the full `log_mel_geometry` values and
the `kws_features` schema version, and the trainer refuses a mismatch;
(3) "rebuild" means: from verified archives into an *empty* store on the M0
Mac, the lock reproduces exactly; on any other toolchain, non-numeric fields
exactly and features within a tolerance measured and written beside the
assertion; (4) separate `log_mel_contract_version` from `kws_features_version`
and add `resampler` (library, version, filter parameters) beside `decoder
versions`; (5) a committed toy manifest — a few hundred kB of CC BY audio from
redistributable sources, never Common Voice (root issue 13) — with committed
expected outputs, and a `kws-dataset` CI job on the `nn-parity` pattern
(`.github/workflows/ci.yml:489–517`) that rebuilds it into an empty store,
asserts the toy lock, refuses a corrupted archive checksum, and rejects each
planted split-leak fixture (root issue 8). The full 300 h rebuild stays a hand
run recorded in `DATASET.md`.

### 4. The hold-out's Pico path has no capture path before M7 and conflicts with M7's replay; the phrase is chosen inside M4 after the builder; §9 says runtime-first took the hold-out off the critical path while M4, M6 and M7 all require it — *major*

**What the plan says.** :499–501: "Recorded through **two microphone paths** —
a host audio interface and the Pico 2 W example's own MEMS microphone — so both
M6's and M7's operating points are measured on the path they ship on."
:573–577: `examples/pico2w/` is M7's. :587–588: M7 plays "The recorded hold-out
set (its Pico-microphone path)" through a loudspeaker. :288–291: the phrase is
"chosen then, recorded in the manifest". :779–781: runtime-first "removes … the
recorded hold-out set and the declared operating point from the critical path".

**What the review found.** No `examples/` directory exists, no I²S/PIO code
exists in MuTap or DspTap, and even M7's firmware as specified records nothing
(§5 :240–242 forbids storing audio) — no milestone builds a capture-only tool.
The two-path requirement is rev 2's own addition (the audit asked for talkers,
matrix, owner and consent row). M4 describes the Pico path as a live recording;
M7 produces it by replay through the board's own microphone; the plan never
says which, and if M7's replay is the measurement, M4's live Pico recording is
redundant. The recordings depend on a phrase M4 chooses only once Piper is in
place; the session protocol is silent on whether SNR is acoustic or post-hoc
mixing, whether the two paths capture each utterance simultaneously, and how the
per-utterance endpoint is obtained. "Development set" is used both for the 50 h
training subset (:305–306, :504) and for what M6's DET reads (:551), and no
line says where the threshold and decision-stage constants are selected. None
of this blocks M5–M8 outright — M6 needs only the host path, M7's replay
measures the Pico operating point on the shipping path — but a consented
session cannot be repeated cheaply.

**What to change.** Split the hold-out by path and by stage: M4 records the
host-interface path only, with a dry close-mic take per utterance and endpoints
annotated on it, and the protocol stated (distances live; SNR realised either
by post-hoc mixing with eval-share noise — said so in the header's
honest-limits block — or acoustically at a metered level); the Pico path is
produced at M7 by the bench protocol's loudspeaker replay, and the consent
row's permitted use covers it; if a live Pico recording is genuinely wanted,
name a capture-only firmware (PIO I²S receive → USB PCM dump) as M7's first
deliverable and re-record in one session. Choose the phrase before the session
(root issue 11). M4's pass requires the specification, consent template,
recording and validation scripts, fixture format and an n = 1 pilot; move
"≥ 10 talkers, ≥ 200 utterances" to M6's pass as the precondition of its
operating point. Add one sentence to Splits: train is what the trainer sees
(including the 50 h selection subset); dev is where architecture, threshold and
decision-stage constants are chosen; eval negatives and the hold-out are read
once per candidate with the dev-chosen threshold. Reword §9 to "removes the
*shipped* hold-out and operating point" with a pointer to §8 :718–723.

### 5. The manifest schema is a provenance-level list; it omits the per-clip row and the fields M5–M8 consume — *major*

**What the plan says.** :492–493 lists six fields. Yet :291 records the phrase
"in the manifest", :481–482 the label form and endpoint tolerance, :119 filters
FMA "by the manifest", :512 wants "every hour of audio accounted for with a
licence", :557 an "attribution block present in the exporter output", :661 a
notices file carrying "the dataset card's attribution text".

**What the review found.** The plan's own sentences presuppose fields the
schema lacks: a phrase field; a licence/role/attribution field per source (per
track for FMA and per file for MUSAN, whose `LICENSE` files are per directory —
verified); a speaker key for TTS positives and AMI; per-clip endpoint and
resolved augmentation parameters (the copied precedent draws every parameter
from one sequential RNG and records only a seed and `"room": "random"` —
`make_dataset.py:243`, :274–279 — so a builder refactor re-rolls the data under
an unchanged manifest); an eval-split identifier the MUKW provenance block
(:126) can cite; a synthesizer block (Piper version, voice sha256, per-clip
speaker id and length/noise scales); the stored path (log vs PCEN) and the
geometry values. "Every hour accounted for with a licence" names no script,
so a hand-written card passes on a missing row. Three attribution copies
(`DATASET.md`, the M6 block, the M8 notices) have no machine-readable origin.
The MUKW provenance block has no defined content or size budget against the
64 KB weights ceiling (:694) — corpus-level ids plus a URI is ~1.3–2.2 KiB and
fits; per-track text does not belong there.

**What to change.** Rewrite the schema paragraph as a versioned document with
three parts: `sources[]` (release id, sha256, size, licence id, attribution
text, terms accepted on download, `redistributable` flag, role —
train / aug / dev / eval-speech / eval-music / holdout — and human obtaining
instructions); `recipe` (geometry values and stored path, label form, endpoint
tolerance in hops, phrase, TTS block, mining method and phonemizer version,
augmentation policy with draws per positive and the negative policy, split rule
with the key per source and a committed salt, exclusion rules); and the
build-emitted `lock` of root issue 3 with the per-clip row. Add
`eval_set_id` = hash of the sorted eval clip list per share, carried into the
MUKW provenance block, immutable after M4 without a version bump. Name
`kws_dataset_card.py`: it generates `DATASET.md` and `ATTRIBUTION.csv` from the
manifest plus decoded durations, with a test that a planted unlicensed clip and
a planted CC BY-NC FMA track fail; the M6 exporter and M8 notices derive from
the same table. State that the MUKW provenance block is corpus-level ids plus
the card's hash/URI, with a byte budget outside the weights ceiling.

### 6. The eval negative set — "the FA/hour denominator for every number in this plan" — has no target hours per share — *major*

**What the plan says.** :486–487: "The eval negative set is named and never
trained on; it is the FA/hour denominator for every number in this plan."
:557–559: M6 targets "≤ 1 false accept per hour … on its speech and its music
share alike." :504–505: "a 50 h development negative set serves architecture
selection, the full corpus only the final DET."

**What the review found.** The only hour figures anywhere are the 300 h
store-sizing example, the 50 h development set (a *training* subset per
:305–306), M7's "one hour", and §7's "hundreds of hours"; no floor exists for
the speech share, and the music share is sized only implicitly (MUSAN's ~42 h
plus an unsized FMA pull). With zero false accepts in *H* hours the one-sided
95 % bound is ln 20 / *H* ≈ 3.0 / *H* FA/h, so a share under 3 h cannot
demonstrate ≤ 1 FA/h at all and M7's 1 h bench bounds only ≤ 3 FA/h; at 1 FA/h,
20 h separates 0.5 from 1.0 with ~79 % power and 50 h gives 0.72–1.28 FA/h
(50 ± 1.96 √50). M4's pass accepts an eval split of any size; the card reports
hours after the fact; M5's report format prints no interval. "The full corpus
only the final DET" reads as conflating the training corpus with the
denominator (its audit source means the full training split). Speech Commands
cannot stand in: its ~3 h of non-`marvin` test clips bound ≤ 1.0 FA/h at zero
events with no margin, and the plan rightly scopes it to bring-up.

**What to change.** Add to Splits and to the pass: "eval negatives ≥ 20 h
speech and ≥ 20 h music (the whole MUSAN music partition plus FMA), fixed and
identical for every run including architecture selection; hours counted from
decoded durations by the manifest script, with the 3.0 / *H* zero-event bound
and the ±1.96 / √*H* relative interval written beside the numbers"; the eval
split is scored once per release candidate. Reword Compute to "50 h of training
negatives for selection runs; the full training split for the final model; one
eval split for both". M5's report prints every FA/h figure with its hours and
exact Poisson 95 % interval. M7: "the 1 h bench bounds ≤ 3 FA/h at zero
events".

### 7. §4 gives MUSAN's music two roles and M4 gives it one; read through M4 no music enters training while M6 is targeted on music; nothing file-level enforces "never used in training" — *major*

**What the plan says.** :118: "Additive noise; its music partition *also*
serves as **evaluation negatives**". :475: "augmentation over MUSAN and the
SLR28 simulated subset" (no partition named). :487–489: the music share is
"MUSAN's music partition and permissively licensed Free Music Archive tracks,
never used in training". :512–513: "splits verified speaker-disjoint by
script".

**What the review found.** Every occurrence of "music" in the plan is on the
evaluation side; M4 names no training-side music, as background or as negative
hours, and gives no reason. The briefing's recipe (:231–232) and openWakeWord's
training (verified) mix music into training, and the repo's own suppressor
history shows the cost of leaving it out (a speech-only v1 lost double-talk
suppression on music and needed a corpus rebuild — `tools/ml/README.md`).
Under §4's "also" reading a builder mixes the same tracks that form the FA/hour
denominator, and the pass cannot notice because music has no speaker key; the
audit asked for "utterance- and speaker-disjoint splits" (:188) and rev 2
carried only the speaker half. MUSAN's music is itself drawn partly from FMA
(verified), so with both in the eval share duplicates double-count the
denominator.

**What to change.** Music becomes its own split axis keyed by (source, artist,
track): a train music share used as additive background under positives and
negatives at a stated SNR range and as music-only negative hours, and the
frozen eval music share, artist-disjoint, de-duplicated between MUSAN's
per-directory `LICENSE` entries and FMA's `tracks.csv` — or an explicit "no
music in training" with its rationale. Correct §4 row 118 to name partitions
("noise and speech partitions for augmentation; music per the M4 split") and
restrict :475 to those partitions. Add to the pass a scripted file-checksum
check that no eval-set file of any source appears in a training or augmentation
pool, beside the speaker check; record the partition table in `DATASET.md`.

### 8. "Speaker-disjoint" names a key for MSWC ↔ Common Voice only, and no planted-leak fixture proves the verifier can fail — *major*

**What the plan says.** :484–485: "train / dev / eval, speaker-disjoint, with
MSWC clips assigned by their Common Voice client id". :512–513: "splits
verified speaker-disjoint by script". :292–298: libritts-high "904 speakers",
kristin and cori single-speaker, john "fine-tuned from Kristin".

**What the review found.** The pass has nothing to check for a TTS clip: the
precedent trainer splits by random permutation (`train_suppressor.py:82–85`),
and `piper-sample-generator` writes sequentially numbered files with no speaker
id (verified), so the inherited default scatters libritts speaker ids across
train and dev and inflates the dev DET M6 selects on; the plan names a dev
*negative* set and never dev positives. AMI ships 24 channels per meeting
(headset, lapel, two arrays — verified) and M4 names neither the stream nor
the split unit; participants take part in several meetings each, so a
meeting-keyed split leaks. MUSAN noise files and SLR28 RIRs have no stated
train/dev partition. MSWC's own splits are speaker-disjoint only *per keyword*
(verified from the paper, §4.5) and must be discarded; whether its `SPEAKER`
column is byte-identical to Common Voice's `client_id` is not established, so a
namespace mismatch would make the cross-corpus check pass vacuously; and
"speaker-disjoint" is really client-id-disjoint (MSWC §3.5 concedes
not-logged-in users appear as several ids). The plan's only planted-defect
test is M5's scoring oracle; the split verifier has none. Impact is bounded —
the declared operating point is measured on the real hold-out — so these leaks
touch dev-based selection and the FA/hour denominator through the MSWC → CV
mapping.

**What to change.** Add to Splits the key per source: Common Voice `client_id`
(joined from MSWC by clip filename into the pinned CV release, the `SPEAKER`
column cross-checked, the agreement rate and unjoinable count recorded,
unjoinable clips excluded from dev/eval); AMI participant set with the stream
named (the single-distant-mic array channel as the device-realistic default;
headset mix optionally as a second labelled condition) and a recording
condition per clip; Piper voice + speaker id per synthesized clip, driven
explicitly and recorded, single-speaker voices placed whole, kristin/john
together as a precaution; Speech Commands' official hash split; MUSAN noise and
SLR28 RIR files partitioned across train/dev by file id. Word the pass
"client-id-disjoint and clip-id-disjoint, verified by script" and record the
approximation in the card. Commit `tools/ml/kws/verify_splits.py` with named
rules R1–R5 and one planted-violation fixture per rule, which the CI job of
root issue 3 asserts is rejected by name.

### 9. Every positive is TTS and every negative is real speech; the augmentation matrix, draws per positive, negative policy, frozen-at-build design and front-end state policy are unstated — *major*

**What the plan says.** :474–476: "Piper synthesis over lineage-verified
voices; augmentation over MUSAN and the SLR28 simulated subset; hard-negative
mining from MSWC by phonetic edit distance; bulk negatives from Common Voice
and AMI." :481: "endpoint tolerance under RIR and tempo augmentation". :493:
"augmentation seeds". :494–495: "derived features for 300 h are ≈ 17 GB
float32". :167–169: PCEN's "initial smoother state and `reset()` semantics as a
contract point".

**What the review found.** No sentence assigns TTS-synthesized *non-target*
phrases to the negative class, so the synthetic-versus-real attribute separates
the classes by construction and the dev DET M6 selects on cannot distinguish
phrase discrimination from vocoder detection. openWakeWord synthesizes
adversarial negatives at parity with positives (verified from its `data.py`
and `train.py`); microWakeWord does not. No SNR range, gain, tempo, RIR subset,
microphone-path model, draws per positive, negative-draw policy or SpecAugment
is stated. The 17.28 GB figure is one unaugmented pass at one geometry
(positives at 50,000 × 1.5 s add ~1.2 GB per draw), and the store plus seeds
imply build-time-frozen waveform augmentation — the copied `make_dataset.py`
pattern — without saying so; that makes every PCEN tune a full rebuild, since
§5 makes PCEN trainer-tunable. The front end primes the PCEN smoother on the
first frame after `reset()` (`log_mel.h:51–53`; the reference :136) with a
time constant of −1/ln 0.975 ≈ 39.5 frames ≈ 0.4 s, so a short positive
featurized in isolation sits inside a transient a continuously running
deployment never sees; frames 0–1 of every clip carry zero history. No policy
chooses between per-clip reset and embedding positives in preceding audio, and
M4 never says which path (log or PCEN) the store holds.

**What to change.** Add an augmentation paragraph to M4: (1) TTS negatives —
for every voice / speaker id used for positives, at least as many non-target
utterances (CC0 sentences, the phrase's words alone, near-miss phrases,
reversed word order) with the same length/noise draws, augmentation and split
assignment; (2) one augmentation distribution applied to both classes with a
stated dry share, ranges as manifest fields (SNR, gain, speed factor with the
endpoint scaled, RIR subset and RT60 range with the direct-path index recorded,
microphone-path model), *K* draws per positive and one clean plus *M* noisy
draws per negative; (3) "augmentation is frozen into the store under the
manifest seed, each clip naming its resolved draw; the trainer applies only
feature-domain masking"; store size = 57.6 MB/h per variant × variants kept;
(4) front-end state policy: positives featurized embedded in ≥ 2 s of same-split
negative material with one reset per mixture, context length in the manifest,
the store's path (plain-log default) named; (5) a builder-emitted per-attribute
class-balance table (source, augmented/dry, voice) as a pass item; (6) a
one-line rule bucketing any transcribed negative that contains the full phrase
and excluding distance-0 MSWC hits.

### 10. The "tracked keyword endpoint" has no definition, no transform through augmentation, no time base, and no stated relation to M5's hit window and §7's 20-hop ceiling — *major*

**What the plan says.** :479–482: "the **label form** — clip-level with a
tracked keyword endpoint, frame/word-aligned, or alignment-free — and the
endpoint tolerance under RIR and tempo augmentation. Recorded in the manifest
and the MUKW header." :518–519: "hit window around each positive's endpoint".
:230 and :696: latency "hops from phrase end", "≤ 20 hops (200 ms)". :218–220:
head shape "decided in M4".

**What the review found.** Nowhere is an endpoint defined for any source (Piper
output with trailing silence; Speech Commands' 1 s clips; hold-out recordings),
nor its transform under a speed factor *f* (*e*/*f*; ±10 % on a 1 s phrase is
±10 hops), an RIR direct-path delay (5 m ≈ 233 samples ≈ 1.5 hops) and a
stream offset; re-trimming by energy *after* RIR convolution moves the endpoint
late by (40/60)·RT60 = 20–67 hops, past the ceiling. The plan already mixes
hops and samples; under `log_mel.h:25–27` an endpoint at sample *n* is first
covered by frame ⌊*n*/160⌋. The audit asked for label form and tolerance in the
milestone *pass* (:248–250); rev 2's pass omits both, so an M4 build with no
endpoints passes. M5's scoring requires an endpoint on every eval positive
whatever loss is trained, so "alignment-free" is not a dataset option; the
tolerance, the hit window and the latency ceiling are three related numbers
with no stated relation (the window's late side must cover ≥ tolerance +
20 hops). The head shape is a model decision. Piper ≥ 1.3.1 exposes
per-phoneme alignments (verified), so word alignment needs no external aligner
if wanted.

**What to change.** Rewrite the paragraph as label *data*: for every positive,
the keyword end sample *e* in the dry source (last 10 ms window above
peak − *X* dB, *X* measured on the four voices), propagated analytically —
*e*′ = round(*e*/*f*), *e*″ = *e*′ + *d* (the RIR's first sample within 40 dB of
its peak, the `make_rir_fixtures.py` rule), *e*‴ = *e*″ + offset — never
re-trimmed after augmentation; stored as a 16 kHz sample index with hop index
⌊*e*/160⌋ under `log_mel`'s alignment; tolerance *T* in hops (the trim rule's
spread across length-scale draws) in the manifest; M5's hit window is
[*e* − *T*, *e* + 20 + *T*] so the §7 ceiling is a scoring rule. Endpoints per
clip for Speech Commands (same trim rule) and the hold-out (annotated on the
close-mic take). Move loss form and head shape to M6 (§5 :218–220 → "decided in
M6 from the label data M4 stores"). Add "label data and tolerance present per
positive" to M4's pass.

### 11. The development phrase is an M4 decision M4 never mentions; its confusability shortlist lapsed with M0; the mining it drives names no G2P, distance, threshold or multi-word rule — *major*

**What the plan says.** :288–291: "then a synthesized four-syllable phrase once
Piper is in place at M4 — chosen then, recorded in the manifest, never
shipped." :274–275: M0's pass "with the phonetic-confusability shortlist if a
phrase is chosen". :475–476: "hard-negative mining from MSWC by phonetic edit
distance". :656: the guide's first topic, "Choosing a phrase (syllables,
confusables)", at M6/M8.

**What the review found.** No line in :471–513 contains "phrase"; the manifest
schema has no phrase field and the hold-out spec does not say the talkers speak
it, yet the hold-out, positives, mining and exclusions all depend on it and the
consented session is one-shot. The promised shortlist exists nowhere. Mining
needs grapheme-to-phoneme on both sides — a lexicon-only source is
out-of-vocabulary on exactly the coined phrases M0 and runtime-first make the
norm, silently yielding an empty hard-negative set the pass would not catch;
MSWC is single 1 s keywords (verified), so a multi-word or invented phrase needs
a per-word or local-alignment rule; the mined list and phonemizer version are
not in the manifest. Piper embeds espeak-ng, so the phonemizer is already in
the toolchain. Thresholds and counts are the builder's to measure (HANDOFF rule
1), not the plan's to fix.

**What to change.** Add a "Phrase" item as M4's first paragraph: candidates
scored by a recorded confusability table (MSWC keywords within phonetic edit
distance ≤ 1 and ≤ 2 using the mining tool, syllables, stress); the chosen
phrase and table recorded in the manifest and dataset card; chosen before the
hold-out session. Specify mining: G2P = espeak-ng phonemes (Piper's; a GPL
tool, listed in the card) with CMUdict where present; distance = phone-level
Levenshtein matched per word of the phrase against MSWC English keywords; the
mined keyword list and phonemizer version stored in the manifest so a lexicon
update cannot move the negative set silently; the builder takes the phrase as
a manifest field and the training-guide script exercises the mining on the toy
corpus. Pass item: "development phrase chosen and recorded with its
confusability table; mined hard-negative set non-empty and listed". Reword
:525 to "M4's phrase".

### 12. The TTS toolchain is mis-described: Piper is now GPL-3.0, synthesis is ONNX Runtime not PyTorch, no runtime estimate or per-speaker budget exists, and all four voices emit 22,050 Hz — *major*

**What the plan says.** :121: "Piper + `piper-sample-generator` | MIT (code)".
:727–728: "TTS synthesis is a PyTorch pipeline of its own". :504–505: "the
trainer gains a `--device` path". :301–302: the `.pt` generator excluded "until
its base voice is verified". Audit :332–333 asked for "TTS synthesis its own
runtime estimate and toolchain list".

**What the review found** (verified on the live pages, 5–8 September 2026).
`rhasspy/piper` was archived on 6 October 2025 ("Development has moved:
OHF-Voice/piper1-gpl"); the maintained line is GPL-3.0 because espeak-ng is
embedded in the wheel, and PyPI `piper-tts` ≥ 1.3 is GPL-3.0-or-later;
`piper-sample-generator` (MIT) pins `piper-tts==1.3.0`, so installing the tool
the row names pulls the GPL Piper. Harmless for the weights — the GPL FAQ is
explicit that program output is not a covered work — but the row a builder
reads is wrong, and the invocation boundary (subprocess, never `import piper`
from distributed code) is undecided. The retained voices are `.onnx` files run
by ONNX Runtime on CPU (Piper exposes only `--cuda`; no CoreML path), so
"PyTorch pipeline" describes the *excluded* `.pt` path, which is the
Lessac-finetuned `libritts_r` checkpoint — "until verified" should read
"excluded: Lessac lineage". At third-party real-time factors of 0.1–0.3,
20,000 × 1.5 s of speech is 50–150 min serial — hours, not days — but
unmeasured on the Mac. No positives-per-speaker budget: uniform-per-voice gives
each libritts id 0.25/904 ≈ 0.03 % against 25 % per single voice. All four
voices are 22,050 Hz; 441:320 to 16 kHz is outside `decimate.h`'s 2/3/6, so the
synthesis path needs its own pinned resampler and the card must say TTS
positives never pass through the shipping decimator. The system Python on the
named Mac is 3.9.6 with no numpy; current numpy/torch/onnxruntime require
≥ 3.10–3.12. `--device` belongs to M6's trainer.

**What to change.** Correct §4 row 121 to "`piper-tts` (GPL-3.0, espeak-ng
embedded; build-time tool invoked as a subprocess, never redistributed; its
output is not a covered work) + `piper-sample-generator` (MIT)"; correct
:727–728 to ONNX Runtime on CPU; reword the `.pt` exclusion. Add to M4: a
build-time tooling table (name, pinned version, licence, role) in the card;
per-clip TTS fields (voice sha256, speaker id, length/noise/noise-w scales, text
variant) in the manifest; *N* positives per libritts speaker id and a floor
share for the single voices; the 22,050 → 16,000 resampler named and pinned; a
process pool; the measured synthesis rate and wall time in M4's Done record;
the Python and package versions pinned. Move `--device mps` to M6 and define
the 50 h development set as a named manifest subset.

### 13. Common Voice's terms changed in October 2025 — account-gated Mozilla Data Collective with a no-re-hosting clause on top of CC0 — and §4, the dataset card and the committed toy negative set do not reflect it — *major*

**What the plan says.** :116: "| Common Voice | CC0; no-reidentification term |
Bulk negatives | yes |". :508/:654: the card carries "the no-reidentification
terms". :656: the toy run "uses a committed tiny negative set rather than
downloading any corpus". :745–747: "M1–M5 are known refactors and known
harnesses."

**What the review found** (Data Consumer terms verified on the live page,
effective 6 May 2026). Downloading requires an account (§1.d); the terms
forbid acquiring a dataset for "hosting, storing or making the Dataset
available on any platform, server or repository other than the Platform"
(§2.b.ix) and any attempt to re-identify contributors (§2.b.viii); the Hugging
Face cards now read "exclusively available through Mozilla Data Collective".
No document in the repo mentions either term, and the rev-1 audit raised only
the re-identification promise, so this is new. M4 as written commits no corpus
audio, so no breach is baked in; but the toy negative set is a committed-audio
item with no named source that could be filled from Common Voice while
satisfying every pass clause; the manifest should record Data Collective
dataset ids (English is now split into accent/gender bundles — American Male
390 h / 9.68 GB, verified) rather than a tarball name; and the download step
needs a one-time human account action that "one command rebuilds … on a clean
checkout" does not acknowledge. MSWC, MUSAN, SLR28 and Speech Commands answer
anonymous requests (verified). The archive volume (~180 GB) is never stated;
the named Mac has ~377 GiB free. §8 files M4 among "known harnesses" and names
no corpus/toolchain access-drift risk, though the briefing (:258–259) warns
that terms change.

**What to change.** Reword §4 row 116: "CC0 licence; Data Collective terms:
account, Data Consumer License, no re-identification, no re-hosting outside the
platform — audio never redistributed; weights yes". Add to M4: `sources[]`
carries the terms accepted and human obtaining instructions; a
`redistributable` flag, with the committed toy set and any committed audio
drawn only from flagged sources (Speech Commands, MUSAN, AMI, MSWC) and the
builder refusing otherwise; the builder never downloads — a `kws_fetch` step
verifies sha256 and size against the manifest and `kws_build` refuses
unverified archives; the pass reads "from verified archives"; a corpus-store
line beside the feature store naming the archive volume and the one account
step. Add a §8 paragraph "Corpus and toolchain access drift" (pinned release
ids, archives retained locally, licence rows dated at verification) and extend
the time-box advice to M4's acquisition and recording.

### 14. The feature store is "a named location outside git" that is never named, has no key or invalidation rule, and the raw-archive and decoded-audio tiers M5–M7 actually consume are unstated — *major*

**What the plan says.** :494–495: "**Feature store:** a named location outside
git (derived features for 300 h are ≈ 17 GB float32); the repo carries
manifests and the builder only." :511–512: "with the feature store mounted".

**What the review found.** The audit (:191) asked for "a named feature store
outside git"; rev 2 restates the requirement without discharging it: no
environment variable or flag, no layout, no rule binding shards to the manifest
revision that produced them (the precedent trainer checks only inter-shard
geometry equality), and the failure mode is already committed twice —
`tools/ml/build_ml_notebook.py:85–86` and `build_head_to_head_notebook.py:90–91`
default the corpus to a prior session's `/tmp` scratchpad. M5's harness, M6's
ABI ("push block", :546) and M7's bench consume samples, not features, so eval
negatives and the hold-out must exist as audio (60 h at 16 kHz int16 ≈ 6.9 GB),
regenerable from the checksummed archives but never said to be. Integrity rules
are absent (verify before extract, filter tar members, never `torch.load` a
`.pt` — the sample generator loads with `weights_only=False`, verified).

**What to change.** Add to the paragraph: `--store` / `MUTAP_KWS_STORE` with no
personal default; the layout `<store>/archives/<source-id>/` (inputs,
sha256-keyed), `<store>/pcm/` (decoded 16 kHz clips keyed by archive digest +
decoder + resampler ids; mandatory for the eval split and hold-out),
`<store>/features/<manifest-hash>/<split>/` with the lock beside them; the
manifest hash covers sources and recipe so any change yields a new directory
and the trainer refuses shards whose embedded hash or contract version differ;
a disk budget (~250 GB) and which tiers may be deleted; one paragraph of
ingestion rules (checksum before extraction, filtered tar members, voices
fetched as `.onnx` + `.json` with pinned sha256 only).

### 15. Stale rev-1 wording and misplaced clauses — *minor*

M5 :524–526 still says Speech Commands bring-up "precedes M0's phrase and
M4's corpus" (M0 chose none; M5 now follows M4), and no milestone owns the
Speech Commands loader, its official validation/testing lists, or an endpoint
convention for its un-annotated clips; M5's pass :533–534 pins "Harness ↔
`kws.h` decision stage parity" though `kws.h` is M6's (the audit's "against
both paths" qualifier was dropped); :504 names a trainer that is M6's; §9
:779–781 contradicts three passes on the hold-out. None blocks M5–M8; each is
one sentence — have M4a ingest Speech Commands v2 (official hash split,
`marvin`/`sheila` with endpoints by the M4 trim rule, its licence row) so M5's
bring-up runs on the builder's output; reword M5 to "starts on M4a's Speech
Commands manifest, is pointed at M4b's splits when they exist"; move the
decision-stage parity clause to M6's pass; move `--device mps` to M6; qualify
§9 as "the *shipped* hold-out and operating point".

## 4. What survives unchanged

Worth saying, since a review lists defects:

- Speaker-disjoint train/dev/eval with MSWC assigned by its Common Voice
  speaker key is the right structure and is executable: the MSWC splits CSV
  carries a per-clip speaker column and its filenames equal Common Voice's
  (verified from the MSWC paper §4.4–4.5), so both a speaker-level and a
  clip-level cross-check are possible.
- One named, never-trained-on eval negative set as the single FA/hour
  denominator, with a music share reported separately, is the correct
  instrument shape and the right carry of audit issue 3.
- The manifest field list is the right skeleton; every amendment extends it.
- The feature-store arithmetic is right: 300 h × 3600 × 100 frames/s × 40 bands
  × 4 B = 17.28 GB; 50 h is 2.88 GB and fits the named Mac; keeping the store
  outside git with manifests and the builder in the repo is the right shape.
- §5's ownership rule is implemented on the engine side exactly as promised:
  every trainer-tunable parameter is a `log_mel_geometry` field exposed through
  the C ABI and `dsptap_py.LogMel`, and `k_contract_version` is readable from
  Python. The numpy reference is the one outlier (root issue 2).
- The numeric chain is measured, not assumed: C++ double vs numpy 1.5e-14 /
  3.4e-14, float vs double 6.7e-7 / 5.3e-6; training on the double path,
  storing float32 and deploying float32 is sound.
- Deciding the label form and endpoint tolerance at M4 is the right milestone;
  what is missing is the endpoint's definition, not its placement.
- The corpus and voice choices are clean and were verified against their
  sources: Common Voice CC0, MSWC CC BY 4.0, AMI CC BY 4.0, MUSAN CC BY 4.0,
  Speech Commands CC BY 4.0, SLR28 Apache 2.0 with the real subset correctly
  gated; all four Piper voice lineages match their model cards (libritts-high
  from scratch on LibriTTS train-clean-360, 904 speakers, CC BY 4.0; kristin and
  cori from scratch on LibriVox public domain; john fine-tuned from kristin);
  `libritts_r` is indeed Lessac-finetuned and the Lessac licence indeed excludes
  speech-recognition products.
- The hold-out design's intent — consented real talkers, a distance × SNR
  matrix, both microphone paths, provenance on the fixture — is sound; only its
  consent scope, protocol, storage and sequencing need deciding.
- The dataset card is the right document and matches the `tools/ml/README.md`
  precedent; the amendments make it generated and testable.
- Three of the four pass clauses can fail on real defects (a licence gap, a
  joined-key leak, a missing consent row); only the rebuild clause cannot.
- The economy decimator's transition band is harmless for the data design:
  computed here, 0.00 dB at 7.12 kHz (band 39's peak), −0.24 dB at 7.4 kHz,
  −1.02 dB only at the 7.6 kHz zero-weight edge, ≈ −0.06 dB band-averaged.
- Sequencing at the M4/M5 boundary is right in spirit: M5 is brought up on
  Speech Commands so it does not wait on gated corpora.

## 5. Findings the review discounted

The cross-lens judge rejected or narrowed fifteen survivors the skeptics let
through; they are recorded so the correction is visible.

| Finding | Why discounted |
|---|---|
| "Under an hour per run" is 38× off | Calibrated on a self-contradictory README figure (2.5 h vs "minutes"), an unnamed machine and a training regime M4 does not own. Residue (record the dev set's size and store format) is in issues 5 and 14. |
| Per-clip split assignment cannot be committed at Common Voice/MSWC scale | Refuted by measurement: a 227,000-row id + split list packs at ~1 MiB, the same class as the notebooks already committed. Residue (rule plus salt) in issue 5. |
| The MUKW provenance block makes §4, §7 and M6 jointly unsatisfiable | Needs per-track text inside the weights payload, which no plan sentence asks for; corpus-level ids plus a URI is ~2 KiB against 64 KB and CC BY 4.0 §3(a)(2) accepts a URI. |
| The corpus resampler must be the shipping economy decimator | Its consequence is ≈ 0.06 dB band-averaged in one band, and `decimate.h` itself designates `transparent` as the offline tier. The surviving "name the resampler" point is in issue 3. |
| Decimating to 16 kHz before the store forecloses measuring the 48 kHz host path | Archives are checksummed and re-decodable; only the Common Voice share is native 48 kHz. Residue belongs to §5/M6. |
| The plan never says which engine computes training features | Contradicted by the plan's own text three times; both engines agree to 1.5e-14. The geometry-reach point is issue 2. |
| Speech Commands is absent from M4's corpus list | Deliberate placement at :114 and :288–291; carried only as the M4a ingestion note in issue 15. |
| Negative pools are taken without a transcript filter | Speculative for a phrase chosen "not a substring of common English", and the error direction is conservative; kept as a one-line rule in issue 9. |
| Word alignment needs a forced aligner | Piper ≥ 1.3.1 exposes per-phoneme alignments (verified); the surviving sentence (track the endpoint for every positive) is issue 10. |
| M5's planted-event oracle needs per-clip rows from M4 | M5's oracle plants its own events (:529–531). Residue duplicates issue 5. |
| The manifest does not name what M5 needs per clip; the trainer uses a random validation split | Duplicate of issue 5; the trainer half is M6's. |
| "Named evaluation set" has no freeze point | Frozen by the committed manifest by construction; the `eval_set_id` residue is one field in issue 5. |
| The feature store is the wrong sole artifact | §7 and §8 already budget the decoded tier; residue (name the tiers) in issue 14. |
| "One command rebuilds" cannot fetch Common Voice or MSWC unattended | Overstated: MSWC's links are checkbox-gated only; Common Voice is scriptable after a one-time account step. Merged into issue 13. |
| §9 contradicts the passes on the hold-out | Real but a duplicate of issue 4. |

## 6. Recommended amendments to the M4 section, in order

For rev 3 of `wake-word-plan.md`:

1. **M4 heading and first paragraph** — stage M4 as M4a / M4b / M4c (§7);
   replace "(the M1 numpy reference, now the real thing)" with the decision of
   issue 2 (DspTap reference-parameterization PR + pin bump as an M4a
   prerequisite, or an explicit reference-geometry freeze for M4–M6); state that
   training features are computed in double and stored float32, and that
   `kws_features.py` asserts band support at the chosen geometry.
2. **New "Phrase" paragraph**, first in M4b — candidates, the measured
   confusability table, the chosen phrase recorded in the manifest and card,
   chosen before the hold-out session; a phrase field in the schema; M5 :525 →
   "M4's phrase" (issue 11).
3. **Mining sentence (:475–476)** — name the G2P, the distance and the per-word
   matching rule; the mined list and phonemizer version stored in the manifest;
   pass item: mined set non-empty and listed (issue 11).
4. **New "Positives and augmentation" paragraph** — TTS negatives; one
   augmentation distribution for both classes with ranges as manifest fields
   and a stated dry share; draws per positive and per negative; augmentation
   frozen into the store under the manifest seed; front-end state policy and
   the store's path; a positives-per-speaker budget; the 22,050 → 16,000
   resampler named and pinned; a class-balance table as a builder output
   (issues 9, 12).
5. **Label paragraph (:479–482)** — rewrite as label *data*: endpoint
   definition, analytic transform through speed/RIR/offset, sample-index time
   base, tolerance *T*, M5's hit window [*e* − *T*, *e* + 20 + *T*]; endpoints
   for Speech Commands and the hold-out by the same rule; loss form and head
   shape move to M6; pass item: label data present per positive (issue 10).
6. **Splits paragraph (:484–490)** — the key per source; "client-id-disjoint
   and clip-id-disjoint, verified by script"; music as its own split axis,
   MUSAN/FMA de-duplicated, with a train music share or an explicit no-music
   rationale; eval negatives ≥ 20 h speech and ≥ 20 h music fixed for every
   run, hours from decoded durations with the bounds beside them; the roles of
   train / dev / eval stated (issues 6, 7, 8).
7. **§4 row 118 (MUSAN)** — "noise and speech partitions for augmentation;
   music per the M4 split"; restrict :475 to those partitions (issue 7).
8. **Manifest schema (:492–493)** — the three-part versioned document
   (`sources[]`, `recipe`, `lock`); `log_mel_contract_version` separate from
   `kws_features_version`; `resampler`, the DspTap commit, the TTS block and
   `eval_set_id` added; split assignment as a rule plus committed salt; no
   demographic columns copied (issues 3, 5).
9. **Feature-store paragraph (:494–495)** — the location mechanism, the three
   tiers, the invalidation rule, a disk budget, ingestion rules (issue 14).
10. **New "Dataset card generation" sentence** — `kws_dataset_card.py` renders
    `DATASET.md` and `ATTRIBUTION.csv` from the manifest; a build-time tooling
    table; MUSAN per-file and FMA per-track attribution merged; the Common Voice
    Data Collective terms recorded; a test that a planted unlicensed clip and a
    planted NC track fail (issues 5, 12, 13).
11. **§4 rows 116 and 121** — Common Voice's Data Collective terms; Piper's
    licence and invocation boundary; the `.pt` clause reworded; §8 :727–728
    corrected to ONNX Runtime on CPU (issues 12, 13).
12. **Hold-out specification (:497–502)** — the consent/licence/privacy
    paragraph of issue 1; the session protocol; the Pico path produced at M7 by
    replay (or a capture-only firmware named as M7's first deliverable); M4's
    pass requires spec, template, scripts, format and an n = 1 pilot; the
    ≥ 10 / ≥ 200 target moves to M6's pass; recall reported with its Wilson
    interval, per-condition counts descriptive (issues 1, 4).
13. **Compute paragraph (:504–505)** — `--device mps` to M6; the 50 h set
    defined as a named manifest subset used for training in selection runs; the
    build stages with a `--jobs` flag; measured wall times and synthesis rate in
    M4's Done record; the pinned Python/package environment (issue 12).
14. **Pass criterion (:511–513)** — replaced by the per-stage criteria of §7.
15. **M5 (:524–527, :533–534)** — "starts on M4a's Speech Commands manifest, is
    pointed at M4b's splits when they exist"; hit window derived from the
    manifest's tolerance plus the §7 ceiling; every FA/h figure printed with its
    hours and Poisson interval and a third "TTS speech" column; the
    decision-stage parity clause moved to M6's pass (issues 6, 10, 15).
16. **M6 pass (:554–560)** — "hold-out ≥ 10 talkers, ≥ 200 utterances per
    microphone path" as the precondition of the declared operating point; the
    trainer's validation set is the manifest's dev split; the exporter reads
    `log_mel_contract_version` and `eval_set_id` from the manifest (issues 3, 4).
17. **M7 bench protocol (:587–591)** — the Pico path of the hold-out is produced
    here by replay through the board's own microphone; the 1 h bench bounds
    ≤ 3 FA/h at zero events (issues 4, 6).
18. **§8** — a "Corpus and toolchain access drift" paragraph and the time-box
    advice extended to M4's acquisition and recording; **§9** qualified to "the
    *shipped* hold-out and operating point" (issues 4, 13).
19. **§5 `tap::mu::kws` list (:216–242)** — the provenance block (corpus-level
    ids plus card hash/URI, byte budget outside the weights ceiling) and the
    endpoint tolerance added to the header field list (issue 5).

## 7. Proposed staging: M4a → M4b → M4c

Each stage has a pass criterion that can fail on a real defect and a committed
fixture, in the §6 manner.

**M4a — Builder, contracts and bring-up corpus** (unblocks M5).
*Prerequisite:* the DspTap reference-parameterization PR + MuTap pin bump, or
the written reference-geometry freeze. *Deliverables:* `kws_features.py`
(geometry dataclass, the imported reference or the bridge, band-support
assertion, parity self-check at the manifest geometry pinned at a measured
tolerance); the three-part manifest schema and the lock format; the builder
with fetch-verify / decode + resample / synthesize / augment / extract / shard
stages, `--store` and `--jobs`, refusing unverified archives;
`verify_splits.py` with rules R1–R5 and one planted-violation fixture each;
`kws_dataset_card.py` with its planted-unlicensed-clip test; Speech Commands v2
ingested under its official hash split with endpoints by the M4 trim rule and
its licence row; a committed toy manifest (≤ 1 MB of CC BY audio from
redistributable sources, never Common Voice) with committed expected lock and
feature vectors; the `kws-dataset` CI job. *Pass:* the toy manifest rebuilds
into an empty store in CI and reproduces the committed lock's non-numeric
fields exactly and its features within the tolerance written beside the
assertion; a corrupted archive checksum is refused; each planted leak fixture is
rejected by name; the planted unlicensed clip and NC track fail the card test;
the parity self-check holds at a non-default geometry (32 bands) if the
reference was parameterized. M5's harness bring-up starts here.

**M4b — The full corpus** (parallel with M5's bring-up). *Deliverables:* the
development phrase with its confusability table, recorded first; Piper
positives and TTS negatives per the augmentation paragraph, with the
per-speaker budget, per-clip TTS fields and the pinned 22,050 → 16,000
resampler; MSWC mining with the mined list stored; Common Voice (Data
Collective ids, the account step documented), AMI (stream named, condition per
clip), MUSAN and FMA (music split by artist/track, de-duplicated) with roles;
splits by the stated key per source; the eval split with ≥ 20 h speech and
≥ 20 h music and its `eval_set_id`; the 50 h selection subset; the
frozen-augmentation store on the M0 Mac; the generated `DATASET.md`, tooling
table and toy-set provenance; measured stage wall times and synthesis rate in
the Done record. *Pass:* the full lock reproduces exactly on the M0 Mac from
verified archives into an empty store; eval hours per share ≥ floor and the
class-balance table show no class-pure attribute, both by script;
`verify_splits` passes on all five rules over all sources; every clip maps to a
licensed, role-tagged source and the card is generated, not typed; label data
and tolerance present for every positive; phrase and mined set recorded.

**M4c — The hold-out, host path** (before or alongside M6's training; nothing
in M5 waits on it). *Deliverables, in this order:* the audio licence and consent
template with the scope of issue 1, pseudonymous ids, off-repo forms, the
repository privacy statement; the fixture format and location with a size
ceiling; the session protocol (distances, SNR method, simultaneous capture if
two paths, dry close-mic take, endpoints annotated on it, optional non-target
read speech); the recording and validation scripts (rate, duration, clipping,
endpoint present, consent id keyed to files); an n = 1 pilot (the owner)
committed with its consent row; the manifest rows for the hold-out. *Pass:* the
builder refuses an utterance whose consent id is missing or whose scope lacks
public redistribution; the pilot's per-file sha256 manifest verifies and the M5
harness scores it by its own hit-window rule end to end; every committed talker
row is pseudonymous and licensed. The ≥ 10 talkers / ≥ 200 utterances target
and the recorded recall with its Wilson interval become M6's pass
precondition; the Pico-microphone path is produced at M7 by loudspeaker replay
through the board's own microphone (the consent template already covers it),
with a capture-only firmware named as M7's first deliverable only if a live Pico
recording is genuinely wanted.

---

### Provenance

- Review only — no code written; the plan, audit and briefing are unchanged.
- Every finding was re-checked by a skeptic against the working checkouts of
  MuTap (`96b4572`) and DspTap (`58210ae`); MuTap-Max, RatioTap and TapTools
  were not available and claims about them are marked unverifiable.
- Root issue 1's evidence was re-verified by hand in the main session:
  `tools/fixtures/make_rir_fixtures.py:108–109` (the MIT stamp) and :216–217
  (the provenance refusal), `LICENSE`, `.gitignore:13–15`, `git lfs` absent,
  no tracked audio, HANDOFF.md:761.
- External facts were checked on the live pages between 5 and 8 September
  2026 by the facts agent (28 facts with URLs, in the session transcript) and
  the two with the largest consequences were re-fetched by hand: the Mozilla
  Data Collective Data Consumer terms (§1.d, §2.b.viii, §2.b.ix) and the
  archive notice on `rhasspy/piper` pointing at `OHF-Voice/piper1-gpl`
  (GPL-3.0, espeak-ng embedded). Speech Commands' split policy is confirmed
  from its paper for the hash-of-file-name rule; that the hashed name carries
  the speaker id is recorded from memory.
- Licence and terms statements are a starting point for diligence at the point
  of use, not a legal opinion.
- The numbers in this document are arithmetic on stated inputs, not
  measurements; each shows its inputs.
