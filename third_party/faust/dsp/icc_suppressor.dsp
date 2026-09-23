// faust-icc's howl suppressor, corrected (icc_48k.lib): three adaptive
// notches, then a single-sideband frequency shifter. Mono in, mono out.
// The anti-howl PoC's baseline row: the notch bank of faust-icc's
// loopManager with its parameters, and freqShift with the x2 restored, the
// modulator order fixed and the guard band a parameter. No gain ceiling.
//
// Runtime entries (defaults in brackets):
//   shift         shift in Hz, positive = up                      [2]
//   guard         pospass guard band in Hz (keep below SR/4)       [100]
//   prom_thresh   howl prominence over the band mean, dB           [15]
//   hold_time     seconds the peak must persist                    [0.20]
//   max_depth     notch depth at full confidence, dB               [12]
//   notch_bypass  1 = notch bank out of the path (it still runs)   [0]
declare name "icc_suppressor";
import("stdfaust.lib");
ic = library("icc_48k.lib");

shift  = nentry("shift",        2,    -20,   20,   0.01);
guard  = nentry("guard",        100,  1,     8000, 1);
prom   = nentry("prom_thresh",  15,   0,     60,   0.1);
hold   = nentry("hold_time",    0.20, 0.001, 10,   0.001);
depth  = nentry("max_depth",    12,   0,     60,   0.1);
bypass = nentry("notch_bypass", 0,    0,     1,    1);

notches = ic.notchBank(3, 32, 150, 6000, 14, prom, hold, 60, depth);
process = _ <: notches, _ : select2(bypass) : ic.freqShift(6, guard, shift);
