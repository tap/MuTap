# The canceller and its knobs

Somewhere in the room, a microphone is listening to a loudspeaker that is
playing what the microphone hears. Raise the gain and at some exact,
repeatable level the system stops being a sound system and becomes an
oscillator: one frequency wins the race around the loop, rings up, and
howls. This chapter is about the object that moves that level — what it
can honestly buy you, what each attribute trades, and how to drive it at
a soundcheck.

Companion material: the object's help patcher
(`help/mutap.defeed~.maxhelp` in the MuTap-Max package) wires a live
mic→speaker loop with every control in this chapter; the
[demo notebook](https://github.com/tap/MuTap/blob/main/notebooks/afc_demo.ipynb)
shows every behavior described here as a measured figure.

## The howl, on a clock

Feedback is not a mystery and not a punishment; it is arithmetic. Sound
leaves the speaker, crosses the room, re-enters the microphone quieter
and later, gets amplified, and goes around again. If one trip around the
loop comes back even slightly *louder* than it left — at any single
frequency — that frequency grows exponentially until something limits it
(usually the amplifier, occasionally the audience).

Two facts to keep from this picture:

- **The threshold is a property of the room + rig, and it is sharp.**
  Engineers call it the **maximum stable gain** (MSG). Below it the loop
  is merely coloring your sound (comb-filtering, a slow "ringy" decay on
  transients); above it, howl. You have felt this: the last 2 dB before
  feedback always sounds *almost* unstable, because it is.
- **The frequency that howls is the loop's choice, not yours.** It is
  wherever the room's echo path has its strongest peak. Move the mic a
  hand's width and the howl frequency jumps.

The whole game of this chapter is measured in one number: **added stable
gain (ASG)** — how many dB *beyond* the room's natural MSG you can go
with the canceller running before howl returns. Every performance claim
below is an ASG measured in the library's closed-loop test rig.

## Two ways to fight it

The classic tool is a **notch filter**: find the howling frequency, cut
it. It works, costs nothing, and has two limits you have also felt.
Every notch takes a bite out of your program material, so a rig that
needs eight notches sounds like a phone call; and a notch fixes the
frequency that howled *last time* — move the mic and you start over.

A **feedback canceller** attacks the loop itself. It listens to what the
patch sends to the speaker, learns the room's speaker→mic echo path as a
filter (an *impulse response* — the same object a convolution reverb
plays), predicts what the speaker's sound will look like when it
re-enters the mic, and **subtracts** that prediction. Nothing is notched
out of your program; what gets removed is a copy of the *speaker's*
signal, at the moment it re-arrives. Cancel the return trip and the loop
gain drops everywhere at once — that drop *is* the added stable gain.

The price of the smarter tool is that it has to *learn* the room, keep
learning it (rooms change: people move, mics move, temperature drifts),
and do so while your program material is playing through everything.
That last part is where the real villain of this chapter lives.

## The wiring

`mutap.defeed~` sits between the microphone and everything else:

```text
 [adc~]                          your processing / mixer
    |                                    |
    +---> (inlet 1: microphone)     [*~ gain]
              [mutap.defeed~ 2048]       |
    +---> (inlet 2: reference) <---------+----> [dac~]  (to the speaker)
```

- **Inlet 1** takes the microphone.
- **Inlet 2** takes the *reference*: the exact signal your patch sends to
  the loudspeaker. Tap it as late in your chain as possible — after the
  gain, after EQ, after anything else the speaker will actually play.
  The canceller can only subtract what it can see coming.
- **Outlet 1** is the cleaned microphone: the mic minus the predicted
  feedback. This is what you process and (eventually) send back out.
- **Outlet 2** reports a number between 0 and 1 every few blocks — the
  double-talk indicator, explained below. Meter it.

One cost, stated plainly: the cleaned output is **delayed by exactly
`@block` samples** (256 by default — 5.3 ms at 48 kHz), on top of your
normal I/O latency. That is the canceller's processing hop, and it is
the only latency the object adds.

## The trap: a loop teaches lies

Here is the thing that separates feedback cancellation from every other
"adaptive filter learns a room" problem, and it is worth one minute of
theory because it explains two of the attributes.

The canceller learns by correlation: *when the speaker did X, what
arrived at the mic shortly after?* In an echo canceller (a phone call),
that works directly, because what the far end says and what you say are
unrelated. But in a feedback loop, the speaker is playing **your own
program material** — the singer the microphone is picking up *is* the
signal coming out of the speaker, a few milliseconds apart. To a
correlation learner, the singer's own voice looks like an echo of the
speaker, because in a loop, it is one.

A naive canceller therefore converges to a lie: a "room" that also
explains the *music*, and subtracting that lie cancels program material
— audibly, as a smeared, phasey version of whatever is sustained — while
the actual feedback protection gets *worse*. In the library's test rig,
the naive learner on sustained tonal material doesn't just fail to add
gain; it **howls 12 dB below the room's own MSG**. Worse than no
canceller at all.

MuTap's answer (the PEM in its algorithm's name, FDAF-PEM-AFROW) is:
before each learning step, build a quick disposable model of what the
*program* currently sounds like — its spectral shape, its pitch — and
filter that predictability out of both signals. What remains is the
unpredictable residue, the part of the sound that genuinely tells you
about the room and not about the song, and the canceller adapts only on
that. The cancellation itself still runs on the full, untouched signals;
only the *learning* is filtered. All of this is automatic and
per-block; you never adjust it. But two attributes (`@warp`, and the
choice of engine) exist because "model what the program sounds like" has
flavors, and it helps to pick the right one.

> **For the curious.** The near-end model is re-fit every block by
> linear prediction on the cleaned output (short-term LPC plus a pitch
> predictor); both the reference and the mic are prefiltered by the
> inverse model before the frequency-domain update — Rombouts, van
> Waterschoot & Moonen's PEM-AFROW (2007) in Gil-Cacho et al.'s
> partitioned frequency-domain form (2014). Because the prewhitening
> filter commutes with the (linear, time-invariant) room, the filter
> learned on whitened signals is still the room's own estimate.

## The knobs, one by one

### Filter length (the creation argument)

`mutap.defeed~ 2048` learns a room echo of up to 2048 samples — about
43 ms at 48 kHz. The rule: **cover the strong part of the room's
speaker→mic response.** For a monitor wedge a meter from the mic, 1024
is plenty; for a PA in a reverberant hall, 4096 or more. Too short and
there is feedback the canceller *cannot represent* — a hard ceiling on
your ASG that no other setting will fix. Too long costs CPU and slows
learning slightly (more filter to estimate from the same signal), but it
does not otherwise hurt. When in doubt, round up.

### `@block` — latency vs. reaction time

The canceller works in blocks: gathers `@block` samples, processes,
plays the cleaned block out. That is your added latency, and also the
rhythm of adaptation (one learning step per block). Smaller blocks react
faster and cut latency; larger blocks are cheaper per sample. The
default 256 (5.3 ms) suits live use. If the rig is latency-critical
(in-ear monitoring), drop to 128 or 64 and spend the CPU; for an
installation that runs unattended, 512 is fine. Powers of two only —
the object rounds up for you. Changing it rebuilds the canceller from
scratch (the learned room resets).

### `@mu` — how hard to chase (the classic engine)

The learning rate of the default (NLMS) engine, between 0 and 2, default
0.5. This is the classic adaptive-filter bargain and it never goes away:

- **High `@mu` (0.5–1):** learns the room fast, tracks a moving mic —
  and keeps twitching once converged, so the residual is noisier and
  sustained material can develop a subtle flutter.
- **Low `@mu` (0.05–0.2):** converges calmly and deeply — and takes its
  time, both at soundcheck and whenever the room changes.

Applied live, no rebuild. If you find yourself resenting this trade-off,
that resentment has a name: it is the reason `@kalman` exists (below).

### `@adapt` — the freeze switch

Off = stop learning, keep cancelling with the room learned so far. The
filter is frozen, not cleared. Two honest uses: freezing a
known-good estimate during a passage where you fear mislearning (a
noise-heavy effects section), and A/B-ing "is the adaptation itself
audible?" One honest warning from the test bench: a frozen estimate
*decays in value* — rooms drift, and much of the measured ASG comes from
the canceller *continuing* to adapt as gain rises. Freeze for moments,
not for shows.

### `@gate` — the double-talk safety layer

On by default. This engages two protections for the classic engine:
learning steps are scaled by how *informative* the current block looks
(the IPC, next paragraph), and blocks that look like sudden near-end
bursts — a cough into the mic, a dropped drumstick — are skipped
entirely, because one such block can wreck a converged filter. The
measured difference is not subtle: in the burst test, the protected
filter sails through a +20 dB burst (worst block RMS ≈ 25) where the
unprotected one blows up four orders of magnitude louder. Leave it on
for anything with a live microphone; turn it off only in controlled
situations where you want the last dB of adaptation speed.

**The IPC outlet** (rightmost) is the gate's sensor, and it is worth a
meter in your patch: it estimates, from 0 to 1, *how much of the current
error is still explainable by the speaker signal*. High = the canceller
is looking at unlearned feedback (learning hard is correct). Low = the
error is your program material (learning would inject the bias from the
trap section). Watching it during a soundcheck tells you when
convergence has actually finished: it starts high and falls toward 0.

### `@warp` — which kind of program material

The disposable program model from the trap section comes in two flavors.
The default models **speech-like** material: a spectral envelope plus one
pitch. Sustained *chords* defeat it — three low strings share no single
pitch, and their tightly packed harmonics are too fine for the default
envelope — so some of the music leaks into the learning and costs gain.
`@warp 1` swaps in a model built for exactly that: a **frequency-warped**
predictor that spends its resolution down low, where musical energy
lives.

Measured on a low chord across eleven simulated rooms: the warped model
holds +7 to +11 dB ASG where the speech model manages between +5 and
*destabilizing* (one room in the test set howls below its own MSG with
the speech model on chord material). On speech it costs nothing. So:
vocal PA, conference, lecture — leave it off; instruments, sustained
pads, choir — turn it on. Changing it rebuilds the canceller. (With the
classic engine, `@warp` quietly keeps part of the `@gate` machinery on
even if you disabled it — the warped model needs that protection to stay
room-robust, and the object refuses to hand you the unstable
combination.)

### `reset` — start over

Zeroes the learned room. Use it after physically reconfiguring the rig
(mic moved to a different stand, speaker repositioned): making the
canceller *unlearn* an old room through adaptation alone takes far
longer than learning a fresh one.

## `@kalman` — the v2 engine

Everything above described the classic engine: an industry-standard
normalized update wearing a hand-tuned step size and a safety gate. The
`@kalman` attribute swaps in a different learner — a per-frequency
**Kalman filter** — and the practical pitch is simple: **it replaces
`@mu` and most of what `@gate` does with two quantities it estimates
continuously**: how uncertain it currently is about the room (per
frequency), and how loud the program currently is (per frequency). Big
uncertainty and quiet program → big confident steps. Converged filter,
loud singer → tiny steps. Automatically, per frequency, with nothing to
set.

What that buys, measured:

- **The `@mu` bargain dissolves.** In noisy conditions the Kalman engine
  converges as fast as the fast setting *and* as deep as the calm one —
  the library's identification test has it 10 dB ahead of the classic
  engine's default at equal time.
- **More gain on broadband material.** On speech-envelope and noise-like
  program the measured ASG doesn't just improve, it saturates the test
  rig's +25 dB probe ceiling (classic stack: +3 to +12.6). On sustained
  chords it holds +8 to +13 dB across every simulated room — with the
  bias protections that the classic engine needs for that material
  simply not needed.
- **Bursts are survived, not suffered.** A +20 dB cough against the
  unprotected Kalman engine momentarily gets loud but the learned room
  *survives* (the classic engine's unprotected filter is destroyed).

With `@kalman 1`, `@mu` is ignored, and `@gate` changes meaning: it
engages a *burst floor* that contains near-end spikes to the same level
as the classic gate. That floor has a measured price — it also throttles
the fast re-learning that happens as a loop approaches instability,
costing roughly 2–6 dB of ASG on sustained tonal material — which is why
you get to choose. The honest guidance:

- **Chasing maximum gain before feedback** (the usual live goal):
  `@kalman 1, @gate 0`.
- **Unattended installation, conference rig, anything where a scare is
  worse than a dB**: `@kalman 1, @gate 1`.

If the Kalman engine is such an improvement, why isn't it simply the
default? Age. The classic engine's behavior has years of literature and,
in this package, the longer test history; the Kalman engine's numbers
are better in every scenario measured so far, and it is expected to
become the default once it has survived real rooms as well as simulated
ones. Nothing stops you from making it your personal default today.

> **For the curious.** A diagonalized partitioned-block frequency-domain
> Kalman filter (Enzner & Vary 2006; Kuech, Mabande & Enzner 2014),
> inside the same PEM prewhitening structure (Bernardi, van Waterschoot,
> Wouters & Moonen). Per bin: state variance P, near-end PSD Ψₛ tracked
> from the residual, gain P·U*/(Σ|U|²P + Ψₛ); process noise
> (1−A²)|W|² keeps P alive after convergence so the filter can track.
> The burst floor swaps Ψₛ for the instantaneous per-bin error power
> when the latter is an outlier (>8× the tracked PSD).

## What to expect, and the soundcheck ritual

Honest expectations, from the measured rig: on speech-like program with
the Kalman engine, the canceller's stable-gain headroom is likely more
than you will ever turn up (the test rig runs out of probe before it
runs out of canceller); on sustained tonal/chordal material — the hard
case for any feedback canceller, because it is the most predictable and
the most bias-prone — expect **+5 to +10 dB of real, usable gain**. That
is the difference between a monitor that is always on the edge and one
with headroom, but it is not infinity; a rig that needs +20 dB on a
sustained organ chord needs a different rig.

The ritual, at soundcheck:

1. Wire the object in, set the filter length for the room, pick the
   engine (`@kalman 1` unless you have a reason not to), `@warp` to
   match the material.
2. Bring the system to a comfortable, clearly-stable gain and let
   program material (or pink noise through the speaker) play for ten to
   twenty seconds. Watch the IPC outlet fall.
3. Raise gain slowly, as you always have. The ring-before-howl still
   tells you where the new edge is — it is simply several dB higher now,
   and the canceller keeps re-learning as you go, which is where much of
   the headroom comes from. Do not freeze adaptation and then push gain:
   the measured ASG assumes the canceller is *live*.
4. Settle 3–6 dB below the new edge, exactly as you would without a
   canceller. The canceller moves the cliff; it does not remove it.

## When it is not the right tool

- **You need one notch and you know where.** A rig with a single stubborn
  resonance and no headroom problems is a job for a parametric EQ, not an
  adaptive filter.
- **The "speaker" signal isn't yours to tap.** The canceller must see the
  exact loudspeaker feed. If the loop closes through equipment you don't
  control, it has no reference to subtract.
- **The path is nonlinear.** A distorting amp or a limiter *inside the
  loop* breaks the "room is a filter" assumption; the canceller will
  chase a moving target. Tap the reference after the nonlinearity if you
  possibly can.
- **Latency budget is zero.** The object costs `@block` samples. At 64
  that is 1.3 ms — but it is not 0.

## Checkpoint

Howl is a loop whose round trip exceeds unity gain at one frequency; the
canceller subtracts the round trip rather than notching the symptom, and
its enemy is the loop's own tendency to make program material look like
echo (the bias that PEM prewhitening removes). Filter length must cover
the room; `@block` is the latency; `@warp` matches the program model to
the material; `@kalman` retires the `@mu` trade-off and upgrades every
measured number, with `@gate` as the burst-hardening choice on either
engine. Expect single-digit honest decibels on the hardest material,
much more on speech — measured, not promised, and re-measured on every
commit by the test suite this chapter cites.
