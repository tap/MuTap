// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors

#include "mutap_capi.h"

#include <new>
#include <variant>

#include "mutap/mutap.h"

namespace {

    template <typename Config>
    void apply_shared_knobs(Config& fdaf_cfg, size_t block_size, size_t partitions, double step_size,
                            double relative_regularization, int ipc_step_scaling, double transient_freeze_ratio) {
        fdaf_cfg.block_size = block_size;
        fdaf_cfg.partitions = partitions;
        if (step_size > 0.0) {
            fdaf_cfg.step_size = step_size;
        }
        if (relative_regularization >= 0.0) {
            fdaf_cfg.relative_regularization = relative_regularization;
        }
        fdaf_cfg.ipc_step_scaling       = ipc_step_scaling != 0;
        fdaf_cfg.transient_freeze_ratio = transient_freeze_ratio;
    }

} // namespace

extern "C" {

struct MutapFdaf {
    mutap::partitioned_fdaf<double> impl;
};

using speech_afc        = mutap::pem_afc<double>;
using warped_afc        = mutap::pem_afc<double, mutap::warped_lpc_predictor<double>>;
using kalman_speech_afc = mutap::pem_afc<double, mutap::speech_predictor<double>, mutap::partitioned_fdkf<double>>;
using kalman_warped_afc = mutap::pem_afc<double, mutap::warped_lpc_predictor<double>, mutap::partitioned_fdkf<double>>;

struct MutapAfc {
    std::variant<speech_afc, warped_afc, kalman_speech_afc, kalman_warped_afc> impl;
};

unsigned mutap_version(void) {
    return MUTAP_VERSION_MAJOR * 10000U + MUTAP_VERSION_MINOR * 100U + MUTAP_VERSION_PATCH;
}

MutapFdaf* mutap_fdaf_create(size_t block_size, size_t partitions, double step_size, double relative_regularization,
                             int ipc_step_scaling, double transient_freeze_ratio) {
    try {
        mutap::partitioned_fdaf<double>::config cfg;
        apply_shared_knobs(cfg, block_size, partitions, step_size, relative_regularization, ipc_step_scaling,
                           transient_freeze_ratio);
        return new MutapFdaf{mutap::partitioned_fdaf<double>(cfg)};
    }
    catch (...) {
        return nullptr;
    }
}

void mutap_fdaf_destroy(MutapFdaf* h) {
    delete h;
}

void mutap_fdaf_process(MutapFdaf* h, const double* input, const double* desired, double* error) {
    if (h != nullptr) {
        h->impl.process_block(input, desired, error);
    }
}

void mutap_fdaf_impulse_response(MutapFdaf* h, double* dest) {
    if (h != nullptr) {
        h->impl.copy_impulse_response(dest);
    }
}

size_t mutap_fdaf_filter_length(const MutapFdaf* h) {
    return h != nullptr ? h->impl.filter_length() : 0;
}

size_t mutap_fdaf_block_size(const MutapFdaf* h) {
    return h != nullptr ? h->impl.block_size() : 0;
}

double mutap_fdaf_ipc(const MutapFdaf* h) {
    return h != nullptr ? h->impl.ipc() : 0.0;
}

void mutap_fdaf_set_step_size(MutapFdaf* h, double mu) {
    if (h != nullptr) {
        h->impl.set_step_size(mu);
    }
}

void mutap_fdaf_set_adaptation(MutapFdaf* h, int enabled) {
    if (h != nullptr) {
        h->impl.set_adaptation(enabled != 0);
    }
}

void mutap_fdaf_reset(MutapFdaf* h) {
    if (h != nullptr) {
        h->impl.reset();
    }
}

MutapFdaf* mutap_fdaf_clone(const MutapFdaf* h) {
    try {
        return h != nullptr ? new MutapFdaf{*h} : nullptr;
    }
    catch (...) {
        return nullptr;
    }
}

MutapAfc* mutap_afc_create(size_t block_size, size_t partitions, double step_size, double relative_regularization,
                           int ipc_step_scaling, double transient_freeze_ratio) {
    try {
        speech_afc::config cfg;
        apply_shared_knobs(cfg.fdaf, block_size, partitions, step_size, relative_regularization, ipc_step_scaling,
                           transient_freeze_ratio);
        return new MutapAfc{speech_afc(cfg)};
    }
    catch (...) {
        return nullptr;
    }
}

MutapAfc* mutap_afc_create_warped(size_t block_size, size_t partitions, double step_size,
                                  double relative_regularization, int ipc_step_scaling, double transient_freeze_ratio,
                                  double lambda, size_t order) {
    try {
        warped_afc::config cfg;
        apply_shared_knobs(cfg.fdaf, block_size, partitions, step_size, relative_regularization, ipc_step_scaling,
                           transient_freeze_ratio);
        if (lambda != 0.0) {
            cfg.predictor.lambda = lambda;
        }
        if (order != 0) {
            cfg.predictor.order = order;
        }
        return new MutapAfc{warped_afc(cfg)};
    }
    catch (...) {
        return nullptr;
    }
}

MutapAfc* mutap_afc_create_kalman(size_t block_size, size_t partitions, double transient_floor_ratio, int warped,
                                  double lambda, size_t order) {
    try {
        if (warped != 0) {
            kalman_warped_afc::config cfg;
            cfg.fdaf.block_size            = block_size;
            cfg.fdaf.partitions            = partitions;
            cfg.fdaf.transient_floor_ratio = transient_floor_ratio;
            if (lambda != 0.0) {
                cfg.predictor.lambda = lambda;
            }
            if (order != 0) {
                cfg.predictor.order = order;
            }
            return new MutapAfc{kalman_warped_afc(cfg)};
        }
        kalman_speech_afc::config cfg;
        cfg.fdaf.block_size            = block_size;
        cfg.fdaf.partitions            = partitions;
        cfg.fdaf.transient_floor_ratio = transient_floor_ratio;
        return new MutapAfc{kalman_speech_afc(cfg)};
    }
    catch (...) {
        return nullptr;
    }
}

void mutap_afc_destroy(MutapAfc* h) {
    delete h;
}

void mutap_afc_process(MutapAfc* h, const double* u, const double* y, double* e) {
    if (h != nullptr) {
        std::visit([&](auto& afc) { afc.process_block(u, y, e); }, h->impl);
    }
}

void mutap_afc_impulse_response(MutapAfc* h, double* dest) {
    if (h != nullptr) {
        std::visit([&](auto& afc) { afc.copy_impulse_response(dest); }, h->impl);
    }
}

size_t mutap_afc_filter_length(const MutapAfc* h) {
    return h != nullptr ? std::visit([](const auto& afc) { return afc.filter_length(); }, h->impl) : 0;
}

size_t mutap_afc_block_size(const MutapAfc* h) {
    return h != nullptr ? std::visit([](const auto& afc) { return afc.block_size(); }, h->impl) : 0;
}

double mutap_afc_ipc(const MutapAfc* h) {
    return h != nullptr ? std::visit([](const auto& afc) { return static_cast<double>(afc.ipc()); }, h->impl) : 0.0;
}

void mutap_afc_set_step_size(MutapAfc* h, double mu) {
    if (h != nullptr) {
        std::visit(
            [&](auto& afc) {
                // The Kalman core has no step size — its gain is the point.
                if constexpr (requires { afc.fdaf().set_step_size(mu); }) {
                    afc.fdaf().set_step_size(mu);
                }
            },
            h->impl);
    }
}

void mutap_afc_set_adaptation(MutapAfc* h, int enabled) {
    if (h != nullptr) {
        std::visit([&](auto& afc) { afc.set_adaptation(enabled != 0); }, h->impl);
    }
}

void mutap_afc_reset(MutapAfc* h) {
    if (h != nullptr) {
        std::visit([](auto& afc) { afc.reset(); }, h->impl);
    }
}

MutapAfc* mutap_afc_clone(const MutapAfc* h) {
    try {
        return h != nullptr ? new MutapAfc{*h} : nullptr;
    }
    catch (...) {
        return nullptr;
    }
}

} // extern "C"
