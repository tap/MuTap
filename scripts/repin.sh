#!/usr/bin/env bash
# The MuTap-Max submodule dance, scripted (HANDOFF.md working note 6).
#
#   scripts/repin.sh [--max-dir DIR] [--no-commit] [REF]
#
# Moves MuTap-Max's submodules/MuTap pin to REF — a MuTap tag (a release) or
# a SHA / branch (between releases; defaults to origin/main's tip) — and
# commits the bump in MuTap-Max with a message in the repo's convention
# ("Bump MuTap to <sha> (<tag or 'main'>; DspTap stays <pin>)").
#
# Rules it enforces, because a dangling gitlink breaks recursive clones:
#   * REF must be reachable from MuTap's origin/main (every MuTap merge is a
#     rebase-merge, so a branch SHA is orphaned the moment its PR merges);
#     a tag must point at such a commit too.
#   * MuTap-Max's working tree must be clean apart from the submodule.
#   * The submodule checkout is updated in place (git -C submodules/MuTap
#     fetch + checkout), so the bump is testable before the push.
#
# It never pushes. Run it from anywhere; it finds MuTap-Max as a sibling of
# this repo (../MuTap-Max) unless --max-dir says otherwise.
set -euo pipefail

mutap_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
max_dir="$mutap_root/../MuTap-Max"
commit=1
ref=""

die() {
    echo "repin.sh: $*" >&2
    exit 1
}

while [ $# -gt 0 ]; do
    case "$1" in
        --max-dir)
            [ $# -ge 2 ] || die "--max-dir needs a directory"
            max_dir="$2"
            shift 2
            ;;
        --no-commit)
            commit=0
            shift
            ;;
        -h | --help)
            sed -n '2,20p' "${BASH_SOURCE[0]}"
            exit 0
            ;;
        -*)
            die "unknown option $1"
            ;;
        *)
            [ -z "$ref" ] || die "one REF only"
            ref="$1"
            shift
            ;;
    esac
done

[ -d "$max_dir/.git" ] || die "no MuTap-Max checkout at $max_dir (use --max-dir)"
[ -f "$max_dir/.gitmodules" ] || die "$max_dir has no .gitmodules"
sub="$max_dir/submodules/MuTap"
[ -d "$sub" ] || die "no submodule checkout at $sub (git submodule update --init first)"

# The pin must be reachable from MuTap's main as origin knows it.
git -C "$mutap_root" fetch --quiet --tags origin
[ -n "$ref" ] || ref="origin/main"
sha="$(git -C "$mutap_root" rev-parse --verify "${ref}^{commit}" 2>/dev/null)" \
    || die "'$ref' is not a commit in $mutap_root"
git -C "$mutap_root" merge-base --is-ancestor "$sha" origin/main \
    || die "$ref ($sha) is not reachable from origin/main: a pin there dangles once its branch is cleaned up"
label="main"
if tag="$(git -C "$mutap_root" describe --tags --exact-match "$sha" 2>/dev/null)"; then
    label="$tag"
fi

# MuTap-Max must be clean apart from the submodule itself.
if [ -n "$(git -C "$max_dir" status --porcelain --ignore-submodules=all)" ]; then
    die "$max_dir has uncommitted changes; commit or stash them first"
fi

old="$(git -C "$max_dir" rev-parse "HEAD:submodules/MuTap")"
if [ "$old" = "$sha" ]; then
    echo "repin.sh: submodules/MuTap already at $sha ($label); nothing to do"
    exit 0
fi

# Move the checkout, then record the gitlink.
git -C "$sub" fetch --quiet --tags origin
git -C "$sub" checkout --quiet --detach "$sha"
git -C "$sub" submodule update --init --recursive --quiet
git -C "$max_dir" add submodules/MuTap

# DspTap's pin, for the message: the convention records that it did or did
# not move with the bump.
dsptap_old="$(git -C "$mutap_root" rev-parse --short "$old:submodules/dsptap" 2>/dev/null || echo unknown)"
dsptap_new="$(git -C "$mutap_root" rev-parse --short "$sha:submodules/dsptap" 2>/dev/null || echo unknown)"
if [ "$dsptap_old" = "$dsptap_new" ]; then
    dsptap_note="DspTap stays $dsptap_new"
else
    dsptap_note="DspTap $dsptap_old -> $dsptap_new"
fi
short="$(git -C "$mutap_root" rev-parse --short "$sha")"
msg="Bump MuTap to $short ($label; $dsptap_note)"

echo "repin.sh: submodules/MuTap $(git -C "$mutap_root" rev-parse --short "$old") -> $short ($label)"
if [ "$commit" -eq 1 ]; then
    git -C "$max_dir" commit --quiet -m "$msg"
    echo "repin.sh: committed in $max_dir: $msg"
    echo "repin.sh: not pushed. Build and run MuTap-Max's tests, then push."
else
    echo "repin.sh: staged, not committed (--no-commit). Suggested message: $msg"
fi
