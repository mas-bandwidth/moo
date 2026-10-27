# moo-l3-buffer-floats

`buffer.readf32`/`readf64`/`writef32`/`writef64` now use the exact boundary rule, in integer
arithmetic, in both the library path and the fastcalls. The four TEMPORARY bridge calls are gone.

## Decision

Reads decode the stored IEEE bits to Q48.16 with `moo_numfromdouble` (the one boundary rule, ties
to even). A stored NaN, infinity, or value outside the number range is not silently mapped: it
raises a named error, one per `moo.h` status (`MOO_NUMBER_NAN`, `MOO_NUMBER_INFINITE`,
`MOO_NUMBER_OUT_OF_RANGE`), with texts `buffer float is NaN`, `buffer float is infinite` and
`buffer float out of number range`. A NaN read silently as 0 would hide a bug, so it must error.
Writes store `moo_numtofloatbits` / `moo_numtodoublebits`, with no float or double arithmetic.

The `readf32` side reads a binary32 value and widens it to binary64 before decoding; the widening is
exact, so it is the same boundary rule as `readf64`.

## What changed

- `VM/src/lbuflib.cpp`: `buffer_readfp` decodes with `moo_numfromdouble` and raises the three named
  errors; `buffer_writefp` stores `moo_numtofloatbits`/`moo_numtodoublebits`. The TEMPORARY bridge
  calls at `:169`/`:179` are removed.
- `VM/src/lbuiltins.cpp`: the `luauF_readfp`/`luauF_writefp` fastcalls give the same results; on any
  status other than `MOO_NUMBER_OK` the read fastcall returns -1 so the library path raises the
  error, and on success it stores the same bits. The TEMPORARY bridge calls at `:1428`/`:1447` are
  removed.
- Tests: `tests/moo/moo-l3-buffer-floats.test.cpp` (`BufferFloatFastcallAgreesWithLibrary`: the
  fastcall, compiled at level 2, and the library path, compiled at level 0, agree bit for bit on
  4096 seeded raw values for `f32` and `f64`) and
  `tests/conformance/moo/l3/moo-l3-buffer-floats.luau` (round trips of 0.1, -0.5 and 2^-16, the
  largest value raising the out-of-range error, a tie rounding to even, and the three named errors
  for both formats).

## Design and DETERMINISM sentences that must change

- DESIGN.md, "Layer 3 as built", "TEMPORARY bridges": the bullet "`buffer.readf32`/`writef32`,
  `readf64`/`writef64` and `string.pack` floats round by the bridge (the boundary PR)" must drop the
  buffer functions -- they now use the exact boundary rule -- leaving `string.pack` floats (and the
  `math`/`vector` bridge) temporary. Suggested replacement: "`string.pack` floats round by the
  bridge (the boundary PR)".
- DETERMINISM.md, 18 ("Float bytes in buffers and string.pack"): `buffer.readf32`/`readf64`/
  `writef32`/`writef64` no longer convert through doubles; they decode/encode in integer arithmetic
  with `moo_numfromdouble`/`moo_numtofloatbits`/`moo_numtodoublebits`, so only `string.pack`'s
  `f`/`d`/`n` formats still convert "between bytes and doubles" as the section's opening sentence
  puts it. The "No FPU is involved" claim now holds for the buffer half.
