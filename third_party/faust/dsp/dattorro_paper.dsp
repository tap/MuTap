// Dattorro's plate (J. Dattorro, "Effect Design, Part 1: Reverberator and
// Other Filters", JAES 45(9), 1997, fig. 1 and table 1) with the paper's delay
// lengths at any sample rate. Mono in, L/R out; the interface is dattorro.dsp's
// (seven runtime entries, paper defaults, pre-delay compile-time 0).
//
// The body of dattorro_paper_rev below is a local copy of FAUST's
// re.dattorro_rev (reverbs.lib 1.5.1, as shipped with FAUST 2.88.0):
//
//   declare dattorro_rev author "Jakob Zerbian";
//   declare dattorro_rev licence "LicenseRef-STK-4.3";
//
// STK-4.3 (MIT-style); see reverbs.lib and THIRD_PARTY_NOTICES.md. Everything
// else in the copy is unchanged. What changed, and why:
//
// 1. The tank's second delay uses the paper's lengths. Upstream's block(i)
//    reads ba.take(i+5, d) for both the second tank allpass and the delay
//    after it (reverbs.lib:906), so the paper's 3720 / 3163-sample delays are
//    never used (the generated code has 1800 and 2656 twice each). Here the
//    delay reads ba.take(i+7, d).
// 2. Every delay length is the paper's sample count at 29761 Hz scaled by
//    ma.SR/29761 and rounded to the nearest sample, so the plate is the
//    paper's size at any rate. Upstream uses the counts unscaled: at 48 kHz
//    that plate is 1.61x smaller. The pre-delay stays in samples (it is 0).
//
// FAUST sizes each delay line from the interval of its length, and ma.SR's
// interval is [1, 192000] (platform.lib), so the lines are sized for 192 kHz
// whatever rate init() is given; see README.md for the resulting footprint.
declare name "dattorro_paper";
import("stdfaust.lib");

dattorro_paper_rev(pre_delay, bw, i_diff1, i_diff2, decay, d_diff1, d_diff2, damping) =
    si.bus(2) : + : *(0.5) : predelay : bw_filter : diffusion_network <: ((si.bus(4) :> _,_) ~ (reverb_network : ro.cross(2)))
with {
    // a delay length from the paper (samples at 29761 Hz) at the running rate
    sr_len(n) = int(n * ma.SR / 29761.0 + 0.5);

    // allpass using delay with fixed size
    allpass_f(t, a) = (+ <: @(t),*(a)) ~ *(-a) : mem,_ : +;

    // input pre-delay and diffusion
    predelay = @(pre_delay);
    bw_filter = *(bw) : +~(mem : *(1-bw));
    diffusion_network = allpass_f(sr_len(142), i_diff1) : allpass_f(sr_len(107), i_diff1)
                      : allpass_f(sr_len(379), i_diff2) : allpass_f(sr_len(277), i_diff2);

    // reverb loop
    reverb_network = par(i, 2, block(i)) with {
        d = (672, 908, 4453, 4217, 1800, 2656, 3720, 3163);
        block(i) = allpass_f(sr_len(ba.take(i+1, d)),-d_diff1) : @(sr_len(ba.take(i+3, d))) : damp :
            allpass_f(sr_len(ba.take(i+5, d)), d_diff2) : @(sr_len(ba.take(i+7, d))) : *(decay)
        with {
            damp = *(1-damping) : +~*(damping) : *(decay);
        };
    };
};

bw      = nentry("bw",      0.9995, 0, 1,      0.0001);
i_diff1 = nentry("i_diff1", 0.75,   0, 1,      0.001);
i_diff2 = nentry("i_diff2", 0.625,  0, 1,      0.001);
decay   = nentry("decay",   0.5,    0, 0.9999, 0.0001);
d_diff1 = nentry("d_diff1", 0.7,    0, 1,      0.001);
d_diff2 = nentry("d_diff2", 0.5,    0, 1,      0.001);
damping = nentry("damping", 0.0005, 0, 1,      0.0001);
process = _ <: dattorro_paper_rev(0, bw, i_diff1, i_diff2, decay, d_diff1, d_diff2, damping);
