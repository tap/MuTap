#!/usr/bin/env bash
# The mutap_fingerprint gate's two shell-side jobs (tests/fingerprint_harness.cpp,
# "Re-recording"):
#
#   scripts/fingerprints.sh check <leg> <ctest args...>
#       Runs the harness through ctest (-R '^mutap_fingerprint$' -V appended),
#       tees its output to fingerprints-out/<leg>.log and passes only when ctest
#       passed AND the output carries "FINGERPRINT_PASS leg=<leg> " (so a build
#       whose leg id is not wired fails instead of comparing nothing). On a
#       failure that printed a FINGERPRINT_NEW block it also writes the
#       commit-ready replacement file fingerprints-out/<leg>.txt, which CI
#       uploads as the artifact "fingerprints-<leg>".
#
#   scripts/fingerprints.sh record <leg> <log> [<out>]
#       Writes <out> (default tests/fingerprints/<leg>.txt) from the
#       FINGERPRINT_NEW block that a failing harness printed for <leg> in
#       <log>: a CI job log as downloaded from GitHub, a ctest -V or
#       --output-on-failure log, or the binary's own output. Only lines tagged
#       with this leg are taken, so a log that holds several legs (the M55 job
#       runs two) or the same leg twice (the hosted Test and Fingerprints
#       steps) is fine; the diff lines and the printed lines are never
#       matched. The new file keeps the old file's description (its comment
#       lines above "# Recorded:") and gets a fresh "# Recorded:" line naming
#       the DspTap pin and, in CI, the run, job and runner image.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

die() {
    echo "fingerprints.sh: $*" >&2
    exit 1
}

check_leg_name() {
    [[ "$1" =~ ^[a-z0-9-]+$ ]] || die "bad leg id '$1' (lowercase letters, digits and '-')"
}

record() {
    local leg="$1" log="$2" out="${3:-$repo_root/tests/fingerprints/$1.txt}"
    check_leg_name "$leg"
    [ -r "$log" ] || die "cannot read log '$log'"
    # The block: "FINGERPRINT_NEW leg=<leg> FINGERPRINT <component> <profile> <hex>",
    # possibly behind a CI timestamp and ctest's "<n>: " prefix, possibly CRLF.
    local lines
    lines=$(tr -d '\r' < "$log" \
        | sed -n "s/^.*FINGERPRINT_NEW leg=$leg \(FINGERPRINT [a-z_]* [a-z]* [0-9a-f]*\)\$/\1/p" \
        | awk '{ key = $2 " " $3
                 if (key in seen) { if (seen[key] != $0) { print "CONFLICT " key > "/dev/stderr"; bad = 1 } ; next }
                 seen[key] = $0; print }
               END { exit bad }') \
        || die "the log holds two different values for one (component, profile) of leg '$leg': the runs in it disagree, so there is nothing sound to record"
    [ -n "$lines" ] || die "no FINGERPRINT_NEW block for leg '$leg' in '$log' (the harness prints it only when the leg fails)"
    # The description: the comment lines above "# Recorded:" of the file
    # being replaced (the committed expectation when writing elsewhere).
    local header="" base="$out"
    [ -f "$base" ] || base="$repo_root/tests/fingerprints/$leg.txt"
    if [ -f "$base" ]; then
        header=$(awk '/^# Recorded:/ { exit } /^#/ { print }' "$base")
    fi
    if [ -z "$header" ]; then
        header="# mutap_fingerprint expectation for CI leg \"$leg\" (MUTAP_FINGERPRINT_LEG=$leg).
# The harness fails this leg on any difference. To change these lines on purpose,
# follow \"Re-recording\" at the top of tests/fingerprint_harness.cpp."
    fi
    local pin source
    pin=$(git -C "$repo_root/submodules/dsptap" rev-parse --short=7 HEAD 2>/dev/null || echo unknown)
    if [ -n "${GITHUB_RUN_ID:-}" ]; then
        source="run ${GITHUB_SERVER_URL:-https://github.com}/${GITHUB_REPOSITORY:-}/actions/runs/$GITHUB_RUN_ID"
        source+=" (job \"${GITHUB_JOB:-?}\", commit ${GITHUB_SHA:0:7}, runner image ${ImageOS:-?} ${ImageVersion:-?})"
    else
        source="$(basename "$log") (recorded with scripts/fingerprints.sh record)"
    fi
    mkdir -p "$(dirname "$out")"
    printf '%s\n# Recorded: DspTap %s, %s.\n%s\n' "$header" "$pin" "$source" "$lines" > "$out"
    echo "fingerprints.sh: wrote $(printf '%s\n' "$lines" | wc -l | tr -d ' ') lines for leg '$leg' to $out"
}

check() {
    local leg="$1"
    shift
    check_leg_name "$leg"
    local dir="fingerprints-out"
    mkdir -p "$dir"
    local log="$dir/$leg.log" status=0
    ctest "$@" -R '^mutap_fingerprint$' -V 2>&1 | tee "$log" || status=$?
    if [ "$status" -eq 0 ] && grep -q "FINGERPRINT_PASS leg=$leg " "$log"; then
        return 0
    fi
    if grep -q "FINGERPRINT_NEW leg=$leg " "$log"; then
        record "$leg" "$log" "$dir/$leg.txt" || true
        echo "::error::mutap_fingerprint: leg '$leg' differs from tests/fingerprints/$leg.txt (diff above). If the change is meant to move output bits, the replacement file is the artifact 'fingerprints-$leg' of this run; see \"Re-recording\" at the top of tests/fingerprint_harness.cpp."
    elif grep -q "FINGERPRINT_PASS leg=$leg " "$log"; then
        echo "::error::mutap_fingerprint: leg '$leg' matched its lines but the test failed afterwards (ctest status $status): a sanitizer report, a crash or a nonzero exit after the verdict (log above)."
    else
        echo "::error::mutap_fingerprint: leg '$leg' did not pass (ctest status $status, no 'FINGERPRINT_PASS leg=$leg ' line and no FINGERPRINT_NEW block): a crash, a timeout, a harness that prints a key twice, or a build whose MUTAP_FINGERPRINT_LEG is not '$leg'."
    fi
    return 1
}

case "${1:-}" in
    check)
        [ $# -ge 2 ] || die "usage: $0 check <leg> <ctest args...>"
        shift
        check "$@"
        ;;
    record)
        [ $# -ge 3 ] && [ $# -le 4 ] || die "usage: $0 record <leg> <log> [<out>]"
        shift
        record "$@"
        ;;
    *)
        die "usage: $0 check <leg> <ctest args...> | record <leg> <log> [<out>]"
        ;;
esac
