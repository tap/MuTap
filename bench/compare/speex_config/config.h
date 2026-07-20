/* Minimal config.h for compiling speexdsp's MDF echo canceller directly
 * (no autotools). Selects the portable float + KISS-FFT configuration.
 * Consumed only by the mutap_speexdsp static library in the comparison
 * harness (bench/compare/CMakeLists.txt, -DMUTAP_COMPARE_SPEEX=ON). */
#pragma once

#define FLOATING_POINT
#define USE_KISS_FFT
#define EXPORT
