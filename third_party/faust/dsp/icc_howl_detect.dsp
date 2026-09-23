// faust-icc's howl detector with its loopManager parameters: 32 log-spaced
// Q-14 bands over 150-6000 Hz, 15 dB prominence, 0.2 s hold. Mono in; three
// outputs: confidence (0..1), peak frequency (Hz), prominence (dB). The
// detector is rate-independent as shipped (its time constants are in
// seconds), so this is upstream's howlDetect, forwarded by icc_48k.lib.
declare name "icc_howl_detect";
import("stdfaust.lib");
ic = library("icc_48k.lib");

process = ic.howlDetect(32, 150, 6000, 14, 15, 0.20);
