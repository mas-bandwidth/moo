#!/bin/sh
# moo: layer 3b's closing gate against fixed3d (docs/DESIGN.md "Conformance (layer 3b)").
#
# Clones mas-bandwidth/fixed3d at FIXED3D_PIN, checks that its vendored fixed headers are identical to moo's
# extern/fixed (so fixed3d's b3* forwarders and moo call the same C), builds
# tools/moo-fixed3d-compare/compare.cpp against fixed3d's headers with BOX3D_FIXED_SATURATE and moo's libraries
# from an existing build directory, and runs it. Exit 1 on any difference.
#
# usage: tools/moo-fixed3d-compare.sh <build dir> [c++ compiler]
#   the build dir must hold libLuau.VM.a, libLuau.Compiler.a, libLuau.Bytecode.a, libLuau.Ast.a,
#   libLuau.Common.a and libMoo.Fixed.a (any target that links the VM and the compiler builds them).
#
# To move the pin: move it with tools/revendor-fixed.sh's FIXED_PIN, to a fixed3d commit that vendors the same fixed.

set -eu

FIXED3D_REPO="https://github.com/mas-bandwidth/fixed3d.git"
# fixed3d main after #111: vendors fixed 8dda4fc, the same pin as tools/revendor-fixed.sh, and BOX3D_FIXED_SATURATE
# defines FIX_SATURATE before fixed/fixed.h is included.
FIXED3D_PIN="ae0e52d8b9c6eb7f4c9496d4050cf0da30e0e00d"

if [ "$#" -lt 1 ]; then
    echo "usage: tools/moo-fixed3d-compare.sh <build dir> [c++ compiler]" >&2
    exit 2
fi

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=$(CDPATH= cd -- "$1" && pwd)
cxx=${2:-${CXX:-c++}}

work=$(mktemp -d "${TMPDIR:-/tmp}/moo-fixed3d-compare.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

git -C "$work" init -q fixed3d
git -C "$work/fixed3d" remote add origin "$FIXED3D_REPO"
if ! git -C "$work/fixed3d" fetch -q --depth 1 origin "$FIXED3D_PIN" 2>/dev/null; then
    echo "moo-fixed3d-compare: fetch of the pinned commit failed; retrying with full history" >&2
    git -C "$work/fixed3d" fetch -q origin
fi
git -C "$work/fixed3d" checkout -q "$FIXED3D_PIN"

if ! diff -r "$work/fixed3d/extern/fixed/include" "$root/extern/fixed/include" >/dev/null; then
    echo "moo-fixed3d-compare: fixed3d $FIXED3D_PIN vendors a different fixed from moo's extern/fixed; move the pins together" >&2
    exit 1
fi
echo "moo-fixed3d-compare: fixed3d $FIXED3D_PIN vendors the same fixed headers as moo"

"$cxx" -std=c++17 -O2 -fwrapv -ffp-contract=off -DBOX3D_FIXED_SATURATE \
    -I"$work/fixed3d/include" -I"$work/fixed3d/extern/fixed/include" \
    -I"$root/VM/include" -I"$root/Compiler/include" -I"$root/Ast/include" -I"$root/Common/include" \
    "$root/tools/moo-fixed3d-compare/compare.cpp" \
    "$build/libLuau.Compiler.a" "$build/libLuau.Bytecode.a" "$build/libLuau.Ast.a" \
    "$build/libLuau.VM.a" "$build/libLuau.Common.a" "$build/libMoo.Fixed.a" \
    -o "$work/fixed3d-compare"

"$work/fixed3d-compare"
