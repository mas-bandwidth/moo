# moo-l4-pow-fractional

Layer 4's `^` with a non-integer exponent: `fixPow` replaces the libm bridge. The integer-valued exponent stays
on `moo_pow_int` (`moo-l4-pow-integer`, `-math`, `-vm`); any other exponent is `fixPow = fixExp2(exponent * fixLog2(base))` (`docs/DESIGN.md`, "pow and ^"). A negative base with a fractional exponent is the named error
`pow of a negative number to a fractional power`; `0 ^ positive fraction = 0`.

## Decision

Adopt `fixPow` for every non-integer exponent. The bridge's reason is gone: the design's non-integer rule names
`fixPow`, which is already integer-only and deterministic (no double inside the VM). The named error surfaces in
`math.pow` (`VM/src/lmathlib.cpp`) and its fastcall (`VM/src/lbuiltins.cpp`, which returns -1 so the library path
raises). The `^` opcode's `luai_numpow` (`VM/src/lnumutils.h`) cannot raise (no `lua_State`), so it releases
`fixPow`'s value, which is 0 for a base <= 0; the opcode's error raising waits for the closing card, as the
integer card's `0 ^ negative` does.

## What changed

- `VM/src/lnumutils.h`: `moo_numpow` now calls `fixPow(a, b)` for a fractional `b` (the TEMPORARY bridge and its
  `moo_numbridge*` call are gone). `#include "fixed/fixed_math.h"` added. `grep -c 'moo_numbridge'` on this file
  is now 0.
- `VM/src/lmathlib.cpp`: `math_pow`'s fractional path raises the named error for `base < 0`, else pushes
  `fixPow(base, exponent)`.
- `VM/src/lbuiltins.cpp`: `luauF_pow`'s fractional path returns -1 for `base < 0` (so `math_pow` raises), else
  sets the result to `fixPow(base, exponent)`.
- `tests/moo/moo-l4-pow-fractional.test.cpp` (new, suite `moo-l4-pow-fractional`):
  - `MooPowFractionalVmMatchesFixPow`: 2^12 seeded (base, fractional exponent) pairs through the `^` opcode and
    `math.pow`, matched bit for bit to `fixPow`; `0 ^ positive fraction = 0`; negative-base opcode releases 0.
  - `MooPowFractionalNegativeBaseError`: `math.pow` raises the named error for a negative base with a fractional
    exponent (both the library and fastcall paths); an integer exponent stays on `moo_pow_int`.
  - `MooPowFractionalMathPowMatchesFixPow`: 2^11 seeded positive-base fractional pairs through `math.pow` match
    `fixPow`.
  - `MooPowFractionalRelativeError`: measures the relative error of `fixPow` against an exact (double, boundary-
    rounded) reference on 2^16 seeded points, bucketed by |exponent|, and prints it.
- `tests/conformance/moo/l3/moo-l4-pow-fractional.luau` (new): pins `0 ^ positive fraction = 0`, the exact
  fractional powers `4 ^ 0.5 = 2` and `9 ^ 0.5 = 3`, `fixPow`'s `2/3/6/8 ^ 0.5` and `9 ^ 1.5 = 1769466` values,
  the named error via `pcall`, and that an integer exponent still takes `moo_pow_int`.

## The measured bound

`fixPow`'s error is relative and grows with |exponent| (DESIGN.md's sentence). `MooPowFractionalRelativeError`
(2^16 seeded points, base in (0, 16], |exponent| in (0, 16]) measured, per |exponent| bucket, the worst relative
error in raw units (2^-16) against the correctly rounded Q48.16 reference:

| |exponent| bucket | worst relative error |
|---|---|---|
| 0.5 | 8.5e-06 |
| 1.0 | 9.6e-06 |
| 2.0 | 1.5e-05 |
| 4.0 | 1.8e-05 |
| 6.0 | 3.7e-05 |
| 8.0 | 4.4e-05 |
| 10.0 | 4.0e-05 |
| 12.0 | 6.0e-05 |
| 14.0 | 6.9e-05 |
| 15.5 | 7.7e-05 |

The worst relative error over the sample is about 7.7e-05 (at |exponent| ~ 15.5). In absolute raw units this is
the known `fixPow` story: the oracle (`moo-l4-math-oracle`) measured maximum raw error about 1.08e14 on its core
pow vectors and about 2.6e14 on all vectors, at results near 2^47 where the same relative error is a large number
of raw units. This is the number the lock records; it is not under one raw unit, so `fixPow`'s bound is a *relative*
bound, as the design says.

## DESIGN.md sentences that must change

- "math library", "`pow` and `^`", the non-integer bullet: add that `fixPow` is what `luai_numpow`, `math.pow`
  and its fastcall now call for a non-integer exponent, the named error is `pow of a negative number to a fractional power`, and the measured bound is the table above (worst relative about 7.7e-05 on the sampled
  range).
- "Layer 3 as built", TEMPORARY bridges: "the libm functions of `math`, `^` and `math.pow` with a fractional
  exponent, `math.noise`, ..." becomes "the libm functions of `math`, `math.noise`, ..." (the fractional-pow
  bridge is gone).

## DETERMINISM.md sentences that must change

- The status table row 7: "`^` and `math.pow` with an integer exponent are integer arithmetic (`moo_pow_int`);
  the other libm functions and a fractional exponent still cross a double bridge" becomes "`^` and `math.pow`
  are integer arithmetic (`moo_pow_int` for an integer exponent, `fixPow` for a fractional one); the other libm
  functions still cross a double bridge".
- Section 7's bullet "`VM/src/lnumutils.h:12` `luai_numpow` is `pow` ..." loses "for a fractional exponent only".

## Not done

- The sibling cards' tests pin the old libm bridge for a fractional exponent: `MathPowFractionalStaysOnBridge`
  (`tests/moo/moo-l4-pow-integer-math.test.cpp`), `MooPowIntVmFractionalBridge`
  (`tests/moo/moo-l4-pow-integer-vm.test.cpp`), and `moo-l4-pow-integer-math.luau` (line 53: `9 ^ 1.5`).
  They are outside this card's PATHS and now fail, and need updating by the closing card (`moo-l4-conformance`).
- The `^` opcode's named error for a negative base with a fractional exponent is not raised (it releases 0); the
  raising site is `VM/src/lvmutils.cpp`/`VM/src/lvmexecute.cpp`, outside this card's PATHS.
- `tools/revendor-fixed.sh --check` passes; the full DONE-WHEN gate was run and the pre-existing
  `MooL5SkippedCasesNameAReason` failure (`moo-l5-suite-audit.test.cpp`, unrelated) remains.
