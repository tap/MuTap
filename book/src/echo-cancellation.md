# The echo canceller

The last chapter fought a loop. This one fights an echo — the same room,
the same speaker-into-microphone physics, with one enormous difference:
**you have the answer sheet.** In an echo problem, the signal coming out
of the loudspeaker is not your own microphone fed back around; it is a
*far end* — the other side of a call, a backing track, a remote performer
— and your patch holds a perfect, clean copy of it. Nothing the canceller
does can come back around and bite it.

That one difference changes almost everything about how hard the problem
is, and the parts it does not change are exactly the parts worth a
chapter. The object is `mutap.aec~`, and it is `mutap.afc~`'s sibling on
purpose: same engines, same attributes, same latency contract. What
changes is which enemy you are configuring against.

Companion material: the object's help patcher
(`help/mutap.aec~.maxhelp` in the MuTap-Max package) demonstrates
everything here **without any acoustic rig** — a delay-and-filter chain
stands in for the room — and section 8 of the
[demo notebook](https://github.com/tap/MuTap/blob/main/notebooks/afc_demo.ipynb)
shows every behavior as a measured figure. Every number in this chapter
comes from the library's open-loop test rig
([`tests/test_aec.cpp`](https://github.com/tap/MuTap/blob/main/tests/test_aec.cpp)).

## The problem, and why it is half as hard

Someone in a conference room talks to a remote caller. The caller's
voice plays from the room's speaker, crosses the room, and lands in the
room's microphone — a few tens of milliseconds late, colored by the walls
— and without help the caller hears themselves come back. That return
trip is the **echo path**, and it is the same physical object the last
chapter's canceller learned: the speaker→mic impulse response.

`mutap.aec~` learns it the same way — *when the reference did X, what
arrived at the mic shortly after?* — and subtracts the prediction. But
because the reference is the far end, not your own output, the loop
never closes. Two consequences:

- **No howl.** The worst an echo canceller can do is cancel badly;
  it cannot turn the room into an oscillator. The stakes drop from
  "the PA screams" to "the caller is annoyed."
- **No bias — *while only the far end talks*.** The last chapter's trap
  (the learner converging to a lie that explains the program material)
  came from the loop making the near end and the reference correlated.
  Cut the loop and, as long as the room is quiet, correlation learning
  just works. Even a naive filter converges: in the test rig, plain
  NLMS on a speech-like far end cancels the echo down to a **~44 dB**
  echo-return-loss enhancement.

So why does this chapter exist? Because rooms are not quiet.

## Double-talk: the trap, relocated

The moment somebody in the room speaks *while* the far end is playing —
which is most moments of any real call — the microphone carries two
things at once: the echo (which the filter must keep explaining) and the
near-end talker (which the filter must not touch). The learner cannot
tell them apart by looking; both are just energy in the error. This is
**double-talk**, and it is to echo cancellation what the bias trap was
to feedback cancellation: the thing that separates a demo from a tool.

The measured damage, from the test rig's protocol — converge on a
speech-like far end, then 600 blocks of double-talk with the near end
*at the same level* as the far end (0 dB, a fair fight), then measure
what is left of the learned room:

- **A naive learner is destroyed.** Its estimate is not merely degraded;
  its misalignment goes *positive* (median **+3.4 dB**) — meaning that
  subtracting its "echo estimate" now adds more energy than it removes.
  Worse than unplugging it.
- **The classical fix is to stop learning.** Bolt on a *double-talk
  detector*, freeze the filter whenever the room speaks. `@gate` on the
  classic engine is that reflex, driven by the IPC: through the same
  double-talk segment the gated filter moves by a measured **±0.1 dB**
  — perfectly protected — while still suppressing the echo it had
  already learned by ~16 dB.
- **MuTap's engines keep learning instead.** The PEM prewhitening from
  the last chapter turns out to be an *echo* technology first (the 2014
  paper it comes from is about double-talk-robust echo cancellation;
  feedback borrowed it later): the disposable model of "what does the
  room's talker sound like" is fit every block and whitened out of the
  update, so the near end stops steering the filter. Through the same
  0 dB double-talk the PEM-NLMS estimate stays deeply useful (median
  **−8.5 dB** misalignment), and the Kalman engine barely moves
  (**−13.3 dB**) while suppressing the echo by a further-measured
  **13.4 dB** *during* the double-talk — with nothing configured, no
  detector, no freeze.

That last line is the chapter's headline. Freezing through double-talk
is safe but blind: a frozen filter cannot track the door opening or the
caller's volume changing while people talk, and people talk constantly.
An engine that adapts *through* double-talk — safely — is the difference
between a canceller for demos and one for conversations.

> **For the curious.** Why does the near end wreck the naive filter in
> open loop, when there is no bias mechanism? Because bias was never the
> only failure mode: the near end is a huge disturbance in the error
> signal, and an NLMS step is proportional to that error. At 0 dB
> double-talk, every block's update is half echo-information, half
> near-end noise, and the noise wins within seconds. PEM whitens the
> near end's *predictable structure* out of the update (dropping its
> disturbing power), and the Kalman engine goes further: its tracked
> per-bin near-end PSD is literally a running double-talk model, so the
> gain collapses in exactly the frequency bins where the talker
> currently is. The IPC (chapter 1) is the same idea as a scalar; the
> Kalman does it per bin, continuously.

## The number that flatters, and the trade underneath

If you A/B a naive canceller against `mutap.aec~` on a *quiet* room —
far end only, nobody talking — the naive one measures **better**: ~44 dB
ERLE against the PEM engines' ~20 dB. It is worth understanding why,
because the same effect shows up in every AEC datasheet ever printed.

A speech-like far end concentrates its energy in a few frequency bands.
The naive learner spends all its effort exactly where the energy is —
its estimate is deep where the signal lives and garbage everywhere else
(its *uniform* misalignment is a shallow −2.6 dB). ERLE, measured on
that same signal, only looks where the energy is, so the naive filter
aces the exam it wrote for itself. The PEM engines whiten the update, so
they spread their effort across the whole band: uniform depth −20 dB,
but only ~20 dB of ERLE on the colored material.

Which is right? For a quiet demo, the naive number. For a room where
anything unexpected happens — the far end changes program, someone
talks — the uniform estimate. The 0 dB double-talk protocol settles it:
the 44 dB filter comes out of the segment *worse than useless* while
the 20 dB filter is still cancelling. The test suite pins both
directions of this trade so neither silently erodes.

## The wiring

```text
 [adc~]  (the room mic: echo + talker)      far end (call, track, remote)
    |                                                |
    +---> (inlet 1: microphone)                 [*~ gain]
              [mutap.aec~ 2048]                      |
    +---> (inlet 2: far-end reference) <-------------+----> [dac~]  (to the speaker)
```

- **Inlet 1**: the microphone.
- **Inlet 2**: the far-end reference — the exact signal your patch sends
  to the loudspeaker, tapped as late in the chain as possible (after the
  fader, after EQ). Same rule as the last chapter: the canceller can
  only subtract what it can see coming.
- **Outlet 1**: the cleaned microphone — the room's talker with the
  far end's echo removed. This is what you send to the far end.
- **Outlet 2**: the IPC, with its meaning flipped by the open loop: high
  while there is unlearned echo to chase, low while the room's talker
  dominates (double-talk). It reports 0 with `@kalman` (whose gating is
  internal and per-bin).

The latency contract is identical to `mutap.afc~`: the cleaned output is
delayed by exactly `@block` samples (256 by default, 5.3 ms at 48 kHz),
and that is the only latency the object adds — until `@postfilter`, which
adds one more block (next section).

## The last 30 dB: `@postfilter`

A linear canceller subtracts its best estimate of the echo, and its best
estimate is never the whole story: the estimate converges asymptotically,
the room drifts, and small nonlinearities in the speaker never fit a
linear filter at all. What survives subtraction is **residual echo** —
far quieter than the original, still audibly *there* on a quiet line.
Telephony standards do not grade "much quieter"; they grade *inaudible*,
with numbers attached.

`@postfilter 1` engages MuTap's answer, and it is a different kind of
machine than everything before it. The canceller *subtracts*; the
post-filter *decides*. Per frequency band, it asks: does what is left in
the microphone still **cohere** with what the canceller believes the echo
is? Where the answer is yes, the leftover is residual echo, and the band
is turned down — by a learned amount, deep only where the evidence is.
Where the answer is no, the leftover is *you*, and the band passes
untouched. Double-talk transparency is not a detector bolted on the side;
it is the shape of the rule, because your voice destroys exactly the
coherence that would justify suppression.

Two companions make the decision maker livable:

- **Comfort noise** (`@comfort`, default on). Turning bands down also
  removes the room tone under them, and a noise floor that breathes —
  present while you talk, gone while the far end talks — is more
  annoying than the echo was. So the post-filter tracks the *real*
  noise floor during pauses (by watching minima, which speech cannot
  fake) and fills what it suppresses back to exactly that level. Fill
  only, never subtraction: turn it off and suppressed bands go silent
  instead.
- **The receive guard.** For the first fraction of a second of a call
  the canceller has learned nothing, its echo estimate is zero, and a
  coherence rule that references it is structurally blind — raw echo
  would pass. The guard applies a modest switched loss (< 14 dB, inside
  the standards' own switching allowance) only while the far end is
  active and convergence is uncertified, then latches off permanently.
  Measured latch: under half a second of far-end speech.

The combination is not tuned to taste; it is the configuration MuTap's
**ITU-T compliance battery** certifies — every requirement of the
in-force automotive/hands-free recommendations (P.1110/P.1120 clause 11,
P.340 full-duplex Category 1, the G.168-adapted battery) met at both
required rates, 48 and 16 kHz. Headlines from the measured tables:
single-talk residual below **−76 dBm0(A)** where the clause wants −58,
double-talk cost to your voice about **1 dB** integrated, comfort noise
matched within ~1–2 dB of the true floor, full-duplex echo loss
**≥ 37 dB in every band** while both sides talk. The full
requirement/measured/margin story — with the trajectories, and the one
documented deviation (deep re-convergence after an *abrupt* path change
is slow; the mask element still holds) — lives in MuTap's
`docs/itu-compliance.md` and the executed proof notebook
`notebooks/itu_compliance.ipynb`.

The fine print. `@postfilter` selects its own engine — the raw Kalman
canceller, because the battery measured the PEM engines dozens of dB
worse on open-loop echo (there is no closed-loop bias for prewhitening
to fix out here, so its predictor refit is pure gradient noise) — which
means `@mu`, `@warp` and `@kalman` are ignored while it is on, and
`@gate` selects the receive guard. The right outlet switches from IPC to
the suppressor's **echo-explained** fraction (0..1 — watch it climb as
the canceller converges; the guard releases at 0.9). And the constrained
gain filter that keeps the suppression click-free costs one extra block
of latency, 10.7 ms total at the defaults.

When to use it: any conversation — calls, conferencing, streams — where
the far end must not hear themselves; this is the product mode. When to
leave it off: measurement and monitoring patches where you want the
linear path untouched, or any time you need the bare canceller's output
to study what it learned.

## The knobs, revisited

Every attribute from chapter 1 exists here with the same mechanics;
what changes is the advice.

- **Filter length (creation arg).** Cover the echo path. Echo setups are
  often *longer* than feedback setups — a conference room's speaker and
  mic are far apart and the tail matters, because an audible echo tail
  is precisely the complaint. The default 2048 (43 ms) suits a small
  room; a live space or a distant ceiling mic wants 4096 or more. Round
  up: too short is a hard ceiling on cancellation that nothing else
  fixes.
- **`@block`.** Same latency-versus-cost trade as chapter 1. Calls
  tolerate more latency than monitor wedges; 256 is comfortable.
- **`@mu`.** The classic engine's learning rate, same bargain as
  chapter 1 — and the same escape hatch: `@kalman` retires it.
- **`@adapt`.** The manual freeze. With a working `@gate` or `@kalman`
  you should rarely need it; it remains the honest A/B switch.
- **`@gate`.** On the classic engine, this is the classical double-talk
  answer: IPC-scaled steps plus the transient freeze. Measured through
  0 dB double-talk it holds the filter to ±0.1 dB — the freeze reflex
  works. Its cost is the flip side: a frozen filter learns nothing,
  and the classic engine *needs* the reflex because its unprotected
  update is the one that gets destroyed. Leave it on with the classic
  engine. With `@kalman` it selects the burst floor instead
  (chapter 1), which matters less here — there is no loop to ring up —
  so the default-off floor is usually right.
- **`@warp`.** Matches the near-end model to **what the room's
  microphone picks up** — not the far end. A vocal booth or conference
  room: leave it off (the speech cascade is built for talkers). A
  stage mic in front of instruments, a room with music playing: turn it
  on. Measured on music-material double-talk with the Kalman engine,
  the warped model won the echo suppression in **every room and seed
  tried** — eighteen out of eighteen — by 2–3 dB in the medians.
- **`@kalman`.** In the feedback chapter this engine was the better
  choice; here it is barely a choice at all. Double-talk is *the*
  failure mode of echo cancellation, and the Kalman engine's whole
  design — per-bin near-end tracking — is a double-talk model. It holds
  the deepest estimate through the 0 dB segment, suppresses the most
  echo during it, needs no detector and no tuning, and re-deepens on
  its own once the room quiets (measured recovery: below −15 dB
  misalignment again within the protocol's single-talk tail).
  `@kalman 1` is the recommended starting point for
  any `mutap.aec~` patch. (The shipping default remains the classic
  engine for the same reason as chapter 1: seniority, and one
  default-engine decision for the package, made once, after real-room
  listening.)
- **`@postfilter`.** The residual-echo suppressor + comfort noise +
  receive guard of the previous section — the ITU-certified chain. On
  for conversation, off for measurement. Ignores `@mu`/`@warp`/`@kalman`
  while on; `@gate` becomes the receive guard; +1 block of latency.
- **`@comfort`.** With `@postfilter` on: fill suppressed bands to the
  room's tracked noise floor (default on). Off = suppressed bands go
  silent — useful when metering how much the suppressor is doing.
- **`reset`.** Same as chapter 1: after physically moving speaker or
  mic, a fresh start beats un-learning.

## What to expect

From the measured rig, with `@kalman 1` on a speech-like far end:
expect the echo to drop out of audibility within a second or two of
far-end speech, roughly 20 dB of broadband suppression once converged,
and — the part that matters — **the cancellation surviving
conversation**: both sides talking at once costs the estimate some
depth (measured: from −20 to −13 dB through a solid 0 dB double-talk
segment), never its usefulness, and it re-deepens on its own the next
time the room is quiet. What an echo canceller cannot do: remove the
room's *own* reverberation from the talker (that is dereverberation, a
different problem), or cancel an echo path it cannot see (a Bluetooth
speaker with its own hidden buffering inserts a variable delay the
filter must re-learn every time it drifts).

## Checkpoint

Echo cancellation is the feedback problem with the loop cut open: same
room-learning machinery, no howl at stake, and no bias — until the room
talks back. Double-talk is where naive learners die (measured: past
useless in one 0 dB segment), where the classical detector-and-freeze
answer merely holds its breath (±0.1 dB, safe but blind), and where
MuTap's engines keep learning — PEM by whitening the talker out of the
update, the Kalman engine by tracking the talker per bin. The flattering
single-talk ERLE of a naive filter is an artifact of excitation-weighted
learning; the uniform estimate is what survives contact with a
conversation. `@kalman 1`, size the filter to the room, tap the
reference post-fader, and meter the IPC — all measured, all pinned by
`tests/test_aec.cpp`, all demonstrable in the help patcher with no rig
at all.
