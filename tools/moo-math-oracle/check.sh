#!/bin/sh
# This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see
# LICENSE-moo.txt for details
#
# Rebuild tests/moo/vectors/math-oracle.bin from the CORE-MATH pin and compare it with the committed file (README.md,
# "Regenerate"). The pin is read from README.md and the file list from CMakeLists.txt, so neither is copied here.
#
# usage: tools/moo-math-oracle/check.sh [COMMITTED.bin]
#
# Exit 0 and print "math-oracle regenerated and identical" when every available compiler rebuilds the file bit for
# bit; exit 1 and print cmp's line and both SHA-256 sums when one does not.
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
committed=${1:-"$root/tests/moo/vectors/math-oracle.bin"}

pin=$(sed -n 's/^C=\([0-9a-f]\{40\}\)$/\1/p' "$script_dir/README.md" | head -n 1)
if [ -z "$pin" ]; then
    echo "check.sh: no CORE-MATH pin (C=<sha>) in $script_dir/README.md" >&2
    exit 1
fi

files=$(sed -n '/^set(MOO_CORE_MATH_FILES$/,/^)/p' "$script_dir/CMakeLists.txt" |
    sed -n 's/^[[:space:]]*\([^[:space:]()]*\)[[:space:]][0-9a-f]\{64\}[[:space:]]*)\{0,1\}$/\1/p')
if [ -z "$files" ]; then
    echo "check.sh: no MOO_CORE_MATH_FILES in $script_dir/CMakeLists.txt" >&2
    exit 1
fi

compilers=
for cc in clang gcc; do
    if command -v "$cc" >/dev/null 2>&1; then
        compilers="$compilers $cc"
    fi
done
if [ -z "$compilers" ]; then
    echo "check.sh: neither clang nor gcc is installed" >&2
    exit 1
fi

scratch=$(mktemp -d "${TMPDIR:-/tmp}/moo-math-oracle.XXXXXX")
trap 'rm -rf "$scratch"' EXIT HUP INT TERM

core="$scratch/core-math"
git init -q "$core"
git -C "$core" remote add origin https://gitlab.inria.fr/core-math/core-math.git
# a partial clone of just the pinned commit; checkout fetches only the files CMakeLists.txt compiles
git -C "$core" -c protocol.version=2 fetch -q --depth 1 --filter=blob:none origin "$pin"
# shellcheck disable=SC2086 # $files is a newline-separated list of paths
git -C "$core" checkout -q FETCH_HEAD -- $(printf 'src/binary64/%s\n' $files)

for cc in $compilers; do
    build="$scratch/build-$cc"
    out="$scratch/math-oracle-$cc.bin"

    cmake -S "$script_dir" -B "$build" -G Ninja -DCORE_MATH_DIR="$core" -DCMAKE_C_COMPILER="$cc" >/dev/null
    cmake --build "$build" >/dev/null
    "$build/moo-math-oracle" "$out" >/dev/null

    if ! cmp -s "$committed" "$out"; then
        cmp "$committed" "$out" || true
        sha256sum "$committed" "$out"
        exit 1
    fi
done

echo "math-oracle regenerated and identical"
