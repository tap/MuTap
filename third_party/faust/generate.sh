#!/usr/bin/env bash
# Regenerate the committed FAUST C++ in generated/ from dsp/*.dsp.
#
# Usage:  third_party/faust/generate.sh          (FAUST=/path/to/faust to override)
#
# Pinned to FAUST 2.88.0: the generated headers are committed, and a different
# compiler (or library set) changes them. Each dsp becomes two headers,
# generated/<name>_f32.hpp and generated/<name>_f64.hpp, whose classes are
# <name>_f32 / <name>_f64 in namespace mutap_faust (faust_shim.h supplies the
# dsp, Meta and UI bases there). -ftz 1 flushes denormals portably; FAUST
# 2.88.0's -ftz 2 output takes the address of an rvalue and does not compile
# with clang.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
faust="${FAUST:-faust}"
want="2.88.0"

if ! command -v "$faust" >/dev/null 2>&1; then
    echo "error: '$faust' not found; install FAUST $want or set FAUST=..." >&2
    exit 1
fi
have="$("$faust" --version | sed -n 's/^FAUST Version \([0-9.]*\).*/\1/p' | head -n 1)"
if [ "$have" != "$want" ]; then
    echo "error: $faust reports FAUST '${have:-unknown}'; the committed headers are FAUST $want" >&2
    exit 1
fi

dsps=(dattorro dattorro_paper icc_suppressor icc_howl_detect)

mkdir -p "$here/generated"
cd "$here"
for name in "${dsps[@]}"; do
    for prec in f32 f64; do
        case "$prec" in
            f32) flag=-single ;;
            f64) flag=-double ;;
        esac
        out="generated/${name}_${prec}.hpp"
        "$faust" -I faust-icc -I dsp -lang cpp "$flag" -ftz 1 -ns mutap_faust \
            -cn "${name}_${prec}" "dsp/${name}.dsp" -o "$out"
        echo "wrote third_party/faust/$out ($(wc -l <"$out" | tr -d ' ') lines, class mutap_faust::${name}_${prec})"
    done
done
