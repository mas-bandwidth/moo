# Vendored `fixed`

A **generated copy** of [mas-bandwidth/fixed](https://github.com/mas-bandwidth/fixed), the
deterministic fixed-point library that moo's `number` is built on (docs/DESIGN.md,
"Semantics").

    Pinned at: unreleased-8dda4fc  (8dda4fcdae38a7fde0a63bd9ba8edc3f2f75ad1c)

fixed main after #66: every unsigned negation is spelled `(T)0 - x`, so MSVC builds it without C4146,
and the Windows `INT128_MIN / 1` divide is defined. This is the commit fixed3d vendors (fixed3d #111),
so a moo program and fixed3d's sim compute with the same C, bit for bit. moo compiles it with `FIX_SATURATE` (CMake target `Moo.Fixed`).

## Do not edit anything in this directory

Every file here except this one is written by `tools/revendor-fixed.sh`. CI runs
`tools/revendor-fixed.sh --check` on every push and fails on any drift, and checks that the
pin line above matches the script. To change this code, change it upstream in
`mas-bandwidth/fixed`, then move the pin in the script and here, in one commit, together with
fixed3d.
