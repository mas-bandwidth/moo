# Vendored `fixed`

A **generated copy** of [mas-bandwidth/fixed](https://github.com/mas-bandwidth/fixed), the
deterministic fixed-point library that moo's `number` is built on (docs/DESIGN.md,
"Semantics").

    Pinned at: v1.6.0  (1df4ea417c865bb623c08b0446e3bd5587721985)

fixed main after #68: the wide (Q112.16) and time (Q32.32) types follow `fixed_t`'s overflow rule
(add, subtract, negate, offset and multiply wrap by default and saturate under `FIX_SATURATE`; division
and narrowing always saturate) and `fixWideMul` / `fixWideDiv` use 256-bit intermediates. `fixed_t`
and its vectors are bit-identical to v1.5.0. This is the commit fixed3d vendors, so a moo program and
fixed3d's sim compute with the same C, bit for bit. moo compiles it with `FIX_SATURATE` (CMake target `Moo.Fixed`).

## Do not edit anything in this directory

Every file here except this one is written by `tools/revendor-fixed.sh`. CI runs
`tools/revendor-fixed.sh --check` on every push and fails on any drift, and checks that the
pin line above matches the script. To change this code, change it upstream in
`mas-bandwidth/fixed`, then move the pin in the script and here, in one commit, together with
fixed3d.
