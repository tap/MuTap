// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors

#include "mutap_capi.h"

#include <new>

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

struct MutapAfc {
    mutap::pem_afc<double> impl;
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
        mutap::pem_afc<double>::config cfg;
        apply_shared_knobs(cfg.fdaf, block_size, partitions, step_size, relative_regularization, ipc_step_scaling,
                           transient_freeze_ratio);
        return new MutapAfc{mutap::pem_afc<double>(cfg)};
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
        h->impl.process_block(u, y, e);
    }
}

void mutap_afc_impulse_response(MutapAfc* h, double* dest) {
    if (h != nullptr) {
        h->impl.copy_impulse_response(dest);
    }
}

size_t mutap_afc_filter_length(const MutapAfc* h) {
    return h != nullptr ? h->impl.filter_length() : 0;
}

size_t mutap_afc_block_size(const MutapAfc* h) {
    return h != nullptr ? h->impl.block_size() : 0;
}

double mutap_afc_ipc(const MutapAfc* h) {
    return h != nullptr ? h->impl.ipc() : 0.0;
}

void mutap_afc_set_step_size(MutapAfc* h, double mu) {
    if (h != nullptr) {
        h->impl.fdaf().set_step_size(mu);
    }
}

void mutap_afc_set_adaptation(MutapAfc* h, int enabled) {
    if (h != nullptr) {
        h->impl.set_adaptation(enabled != 0);
    }
}

void mutap_afc_reset(MutapAfc* h) {
    if (h != nullptr) {
        h->impl.reset();
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
