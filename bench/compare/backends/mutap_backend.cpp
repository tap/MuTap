// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
#include "mutap_backend.h"

namespace mutap_compare {

    void register_mutap_backends() {
        register_subject({"mutap", "MuTap certified aec_chain (FD-Kalman + suppressor + CN), float64",
                          [](double fs) -> std::unique_ptr<aec_backend> {
                              return std::make_unique<mutap_chain_backend<double>>(fs);
                          }});
        register_subject({"mutap-f32", "MuTap certified aec_chain at float32 (deployment precision)",
                          [](double fs) -> std::unique_ptr<aec_backend> {
                              return std::make_unique<mutap_chain_backend<float>>(fs);
                          }});
        register_subject({"mutap-linear", "MuTap raw FD-Kalman canceller alone (no post-filter)",
                          [](double fs) -> std::unique_ptr<aec_backend> {
                              return std::make_unique<mutap_linear_backend<double>>(fs);
                          }});
    }

} // namespace mutap_compare
