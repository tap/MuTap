// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
#include "aec_backend.h"

namespace mutap_compare {

    std::vector<subject>& registry() {
        static std::vector<subject> r;
        return r;
    }

    bool register_subject(subject s) {
        registry().push_back(std::move(s));
        return true;
    }

} // namespace mutap_compare
