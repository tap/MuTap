# Introduction

Point a microphone at a loudspeaker that is playing that microphone, and
you have built an oscillator. Every PA operator, hearing-aid wearer,
teleconference participant and guitarist standing too close to the amp
knows the sound it makes.

This book is about *un*-building that oscillator without turning anything
down. Its vehicle is [**MuTap**](https://github.com/tap/MuTap), an
open-source library of adaptive audio-cleaning filters, through its
Max/MSP package [**MuTap-Max**](https://github.com/tap/MuTap-Max) — but
the ideas (why loops howl, why the obvious fix is biased, what an
adaptive canceller can and cannot buy you) transfer to any feedback
canceller you will ever meet.

It is written for people who make sound, not people who prove
convergence theorems. The mathematics underneath is real and the text
will always say where it lives, but the main thread works in situations,
knobs and trade-offs; the formulas stay in clearly marked *for the
curious* sidebars you can skip without losing anything you need at the
mixing desk.

Like its sibling books ([AmbiTap](https://github.com/tap/AmbiTap),
[SampleRateTap](https://github.com/tap/SampleRateTap)), it tries to stay
mechanically honest: every performance number quoted in these pages is
measured by the library's own test suite or its
[companion notebook](https://github.com/tap/MuTap/blob/main/notebooks/afc_demo.ipynb),
which drives the very C++ code the shipping objects run. When a chapter
says a setting trades six decibels for burst-proofing, that six is a
regression-tested six, not a vibe.

This is an early draft — two chapters so far: the acoustic feedback
canceller (`mutap.afc~`) end to end, and its open-loop cousin, the echo
canceller (`mutap.aec~`). A chapter on the embedded targets will join
them as that work lands.
