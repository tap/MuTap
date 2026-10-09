// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// README.md's quick start, compiled and run (the production-readiness plan's
// M0a). The snippets are not copied here: tests/CMakeLists.txt extracts
// every ```cpp block of README.md at configure time into
// readme_snippets_generated.h — its #include lines hoisted, its statements
// wrapped as readme_snippets::snippet_<n>() — so the README is the only
// source, and a snippet that names a type or namespace that does not exist
// fails this build on every leg (the namespace is tap::mu; README quoted a
// `mutap::` that never existed until this test). Runs in both emulated
// selections: the snippets process one block of zeros each.

#include <cstddef>

#include <gtest/gtest.h>

#include "readme_snippets_generated.h"

namespace {

    TEST(ReadmeSnippets, QuickStartCompilesAndRuns) {
        static_assert(readme_snippets::k_count == 2,
                      "README.md's quick start has a different number of ```cpp blocks than this test expects: "
                      "update the count here and call the new snippet below");
        readme_snippets::snippet_1(); // the FFT
        readme_snippets::snippet_2(); // the feedback canceller
        SUCCEED();
    }

} // namespace
