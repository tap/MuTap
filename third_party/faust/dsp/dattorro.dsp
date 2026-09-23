// FAUST's re.dattorro_rev as a mono-in / stereo-out block with every
// parameter a runtime entry. Defaults are re.dattorro_rev_default's (the
// paper's). Pre-delay is compile-time 0.
declare name "dattorro";
import("stdfaust.lib");
bw      = nentry("bw",      0.9995, 0, 1,      0.0001);
i_diff1 = nentry("i_diff1", 0.75,   0, 1,      0.001);
i_diff2 = nentry("i_diff2", 0.625,  0, 1,      0.001);
decay   = nentry("decay",   0.5,    0, 0.9999, 0.0001);
d_diff1 = nentry("d_diff1", 0.7,    0, 1,      0.001);
d_diff2 = nentry("d_diff2", 0.5,    0, 1,      0.001);
damping = nentry("damping", 0.0005, 0, 1,      0.0001);
process = _ <: re.dattorro_rev(0, bw, i_diff1, i_diff2, decay, d_diff1, d_diff2, damping);
