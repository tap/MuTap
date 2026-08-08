/// @file nn_chain.h
/// @brief The AEC chain composed with the learned post-filter, and its
///        preset: aec_chain's canceller/guard/rescue/shadow calibration
///        with nn_suppressor as the Post engine.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors

#pragma once

#include <cmath>
#include <utility>

#include "mutap/nn_suppressor.h"
#include "mutap/postfilter.h"

namespace tap::mu {

    /// The learned-post chain: raw FD-Kalman canceller + nn_suppressor.
    template <typename Sample>
    using aec_chain_nn = aec_chain<Sample, partitioned_fdkf<Sample>, nn_suppressor<Sample>>;

    /// The learned-engine preset: identical canceller and chain-level
    /// calibration to aec_chain_preset (same geometry-ratio rescaling),
    /// with the post stage swapped for the trained model. The weights'
    /// hop must equal block_size (nn_suppressor validates). The noise-
    /// floor tracker constants rescale exactly as the classical
    /// suppressor's do, so comfort noise behaves the same at any
    /// (block_size, sample_rate).
    template <typename Sample>
    typename aec_chain_nn<Sample>::config aec_chain_nn_preset(size_t block_size, size_t partitions, double sample_rate,
                                                              nn_suppressor_weights weights) {
        const auto classic = aec_chain_preset<Sample>(block_size, partitions, sample_rate);

        typename aec_chain_nn<Sample>::config cfg;
        cfg.canceller              = classic.canceller;
        cfg.guard_attenuation_db   = classic.guard_attenuation_db;
        cfg.guard_activity_ratio   = classic.guard_activity_ratio;
        cfg.guard_converge_ratio   = classic.guard_converge_ratio;
        cfg.guard_hold_blocks      = classic.guard_hold_blocks;
        cfg.rescue_drop_ratio      = classic.rescue_drop_ratio;
        cfg.rescue_hold_blocks     = classic.rescue_hold_blocks;
        cfg.rescue_cooldown_blocks = classic.rescue_cooldown_blocks;
        cfg.shadow_partitions      = classic.shadow_partitions;
        cfg.shadow_transition      = classic.shadow_transition;
        cfg.shadow_ratio           = classic.shadow_ratio;
        cfg.shadow_hold_blocks     = classic.shadow_hold_blocks;

        const double hop_ref   = 256.0 / 48000.0; // the certified reference geometry
        const double ratio     = (static_cast<double>(block_size) / sample_rate) / hop_ref;
        auto&        pf        = cfg.postfilter;
        pf.block_size          = block_size;
        pf.floor_smoothing     = static_cast<Sample>(std::pow(0.9, ratio));
        pf.floor_window        = std::max<size_t>(8, static_cast<size_t>(128.0 / ratio));
        pf.explained_smoothing = static_cast<Sample>(std::pow(0.95, ratio));
        pf.weights             = std::move(weights);
        return cfg;
    }

} // namespace tap::mu
