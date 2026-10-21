# moo-l3-boundary-tofloat

The out-of-VM boundary now rounds to the nearest float bit pattern, ties to even, in integer
arithmetic, so `moo_todouble` and a new `moo_tofloat` share one rule.

## Decision

`moo_tofloat` must exist: DESIGN.md "Boundary" already says `buffer.writef32`/`writef64` and
`string.pack` encode to the nearest float bit pattern, and that encode is the same boundary rule
as `moo_todouble`. moo.h had `moo_fromdouble`, `moo_fromfloat` and `moo_todouble` but no
`moo_tofloat`; `float(moo_todouble(n))` would round twice (once to double, once to float). So both
encodes are built from one pair of integer-arithmetic helpers.

## What changed

- `Common/include/moo_number.h`: add `moo_numtofloatbits(fixed_t)` and
  `moo_numtodoublebits(fixed_t)`. Each normalizes the magnitude, rounds the mantissa once, ties to
  even, and assembles sign, exponent and mantissa; `0` gives `+0.0`'s bits. Every Q48.16 value is
  a finite normal binary32/binary64 (the domain is `[-INT64_MAX, INT64_MAX]` raw = about `2^47`,
  and the least nonzero value is `2^-16`), so there is no subnormal, infinity or overflow case.
- `VM/include/moo.h` and `VM/src/moo_api.cpp`: add `LUA_API float moo_tofloat(int64_t n)`, a
  `memcpy` of the bits; `moo_todouble` is rebuilt on `moo_numtodoublebits` so the two paths are one.
- Tests: `tests/moo/moo-l3-boundary-tofloat.test.cpp` (`BoundaryToFloatIsNearestTiesToEven`,
  `BoundaryToFloatSeeded`) and `tests/conformance/moo/l3/moo-l3-boundary-tofloat.luau`.

## Design and DETERMINISM sentences that must change

- DESIGN.md, "Boundary": the `moo_todouble` bullet gains `moo_tofloat` with the same rule, and the
  `buffer`/`string.pack` sentence ("encode to the nearest float bit pattern, ties to even") now
  names `moo_tofloat` / `moo_numtofloatbits` as the rule, where it said the bridge bounds it.
  Suggested text: "`moo_todouble(fixed_t)` and `moo_tofloat(int64_t)` are exact for magnitudes below
  2^37; above that, round to nearest even, in integer arithmetic." and "The boundary rule is
  `moo_numtodoublebits`/`moo_numtofloatbits`; `writef32`/`writef64` and `string.pack` encode through
  it."
- DETERMINISM.md, 18 ("Float bytes in buffers and string.pack"): the sentence "encode Q48.16 to the
  nearest float bit pattern the same way. No FPU is involved." is unchanged; it is now true because
  the encode is `moo_numtofloatbits`/`moo_numtodoublebits`, integer arithmetic.
- DESIGN.md, "Layer 3 as built", "TEMPORARY bridges": the `buffer.readf32`/`writef32` and
  `string.pack` bullet currently says "round by the bridge (the boundary PR)". The closing card may
  move it off the temporary-bridge list, since the encode rule is now a permanent integer path.
