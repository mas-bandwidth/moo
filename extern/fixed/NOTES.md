# Vendored `fixed`

A **generated copy** of [mas-bandwidth/fixed](https://github.com/mas-bandwidth/fixed), the
deterministic fixed-point library that moo's `number` is built on (docs/DESIGN.md,
"Semantics").

    Pinned at: unreleased-4a70c64  (4a70c64eaedf58cb6363ad7a3a48387d2ff15502)

This is the commit fixed3d vendors, so a moo program and fixed3d's sim compute with the same C,
bit for bit. moo compiles it with `FIX_SATURATE` (CMake target `Moo.Fixed`).

## Do not edit anything in this directory

Every file here except this one is written by `tools/revendor-fixed.sh`. CI runs
`tools/revendor-fixed.sh --check` on every push and fails on any drift, and checks that the
pin line above matches the script. To change this code, change it upstream in
`mas-bandwidth/fixed`, then move the pin in the script and here, in one commit, together with
fixed3d.
