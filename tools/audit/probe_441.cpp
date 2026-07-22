// Follow-up: is the 44.1 kHz / block 256 early-convergence deficit the same
// rank-deficiency notch the preset's countermeasures close in the 6..12 ms
// hop band? Enable them manually at 44.1 kHz and compare ERL trajectories.
#include <cstdio>

#include "support/itu_chain.h"

using namespace mutap_test;
using namespace mutap_test::itu;

int main() {
    rate_setup rs{44100.0, 256, 2048};
    const auto path = compliance_path(room::cabin, rs);
    css_config cc;
    cc.periods = 12;
    cc.shaped  = true;
    auto x     = make_css_at(cc, rs.fs);
    set_level_dbm0(x, -16.0);

    {
        compliance_dut<double> c(chain_config<double>(rs), rs.block);
        auto                   rr = run_chain(c, path, rs.block, x);
        erl_reader             erl(rr.echo, rr.out, rs.fs);
        std::printf("stock preset (novelty off):  ERL 600ms %5.1f  1200ms %5.1f  2000ms %5.1f\n", erl.by(0.6),
                    erl.by(1.2), erl.by(2.0));
    }
    {
        auto cfg                                = chain_config<double>(rs);
        cfg.canceller.novelty_smoothing         = 0.8;
        cfg.canceller.novelty_floor             = 0.1;
        cfg.canceller.initial_uncertainty_decay = 0.5;
        compliance_dut<double> c(cfg, rs.block);
        auto                   rr = run_chain(c, path, rs.block, x);
        erl_reader             erl(rr.echo, rr.out, rs.fs);
        std::printf("with notch countermeasures:  ERL 600ms %5.1f  1200ms %5.1f  2000ms %5.1f\n", erl.by(0.6),
                    erl.by(1.2), erl.by(2.0));
    }
    // Control: the certified 48 kHz geometry through the same code path.
    {
        rate_setup r48 = setup_48k();
        const auto p48 = compliance_path(room::cabin, r48);
        css_config c8;
        c8.periods = 12;
        c8.shaped  = true;
        auto x48   = make_css_at(c8, r48.fs);
        set_level_dbm0(x48, -16.0);
        compliance_dut<double> c(chain_config<double>(r48), r48.block);
        auto                   rr = run_chain(c, p48, r48.block, x48);
        erl_reader             erl(rr.echo, rr.out, r48.fs);
        std::printf("48 kHz control:              ERL 600ms %5.1f  1200ms %5.1f  2000ms %5.1f\n", erl.by(0.6),
                    erl.by(1.2), erl.by(2.0));
    }
    return 0;
}
