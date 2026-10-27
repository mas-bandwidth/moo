# moo-l4-conformance: layer 4's closing gate

Layer 4 is checked by the CORE-MATH error table, round-trip of every formatted value, and bit-exact
agreement with `fixed` (docs/DESIGN.md, "Layers", item 4). This card measures every function in the frozen
oracle file against the composition the "math library" table names, turns each measured maximum into an
assertion, adds a determinism program over the whole math library and `^`, and collects every layer 4 note's
document changes. It changes no VM, compiler, formatter or `moo_number.h` code.

## Decisions

1. **The bounds are the measured maxima.** The table below is produced by
   `tests/moo/moo-l4-conformance.test.cpp`, `TEST_CASE("MooL4ConformanceMathTable")`, over the same
   `tests/moo/vectors/math-oracle.bin` that `moo-l4-math-oracle` froze (CORE-MATH `dae5dd8c`, 65536 PCG32
   draws per function, asin/acos exhaustive over [-1, 1]). Each function's source is the exact composition
   DESIGN.md names, built from `fixed`'s C called directly; the value is the maximum of `got - ideal` in raw
   units and `count` is the number of records at that maximum. The same numbers are asserted as bounds, so a
   change in `fixed` or in a composition that moves the maximum fails the test rather than drifting silently.
2. **The table replaces "not yet measured".** DESIGN.md "math library", "Accuracy oracle" is to say:

   > The oracle is `tests/moo/vectors/math-oracle.bin`, generated offline by `tools/moo-math-oracle` from
   > CORE-MATH at a pinned commit and frozen. `tests/moo/moo-l4-conformance.test.cpp` measures the fixed
   > composition of every function and asserts the bound in the table below.

   and its paragraph "Known today from `fixed`'s own documentation ... The other bounds are not yet measured
   and are not claimed." becomes the measured table and "the measured maximum is the bound".

| function | sample (records) | max error (raw) | count at max |
|---|---:|---:|---:|
| `sin` | 49,367 | 131,066 (negative, large argument) | 1 |
| `cos` | 49,304 | 131,068 (negative, large argument) | 1 |
| `tan` | 49,336 (nonzero cosine) | 16,661,531,715 | 2 |
| `asin` | 131,073 | 4 (negative) | 18 |
| `acos` | 131,073 | 4 (positive) | 18 |
| `atan` | 49,483 | 2 (negative) | 141 |
| `atan2` | 63,138 | 2 (positive) | 1,397 |
| `exp` | 47,337 | 1,503,472,452,145,152 (negative, near the top of the range) | 1 |
| `log` | 47,156 | 11 (positive) | 243 |
| `log10` | 47,115 | 15 (negative) | 689 |
| `log2` | 47,044 | 0 | 47,044 |
| `sinh` | 47,349 | 1,498,395,532,686,336 (positive) | 1 |
| `cosh` | 47,327 | 1,174,843,021,516,800 (negative) | 1 |
| `tanh` | 47,405 | 2 (positive) | 284 |
| `sqrt` | 47,088 | 1 (negative; rounds toward zero) | 23,494 |
| `pow` (base > 0) | 32,073 | 258,932,934,890,496 (negative, result near 2^47) | 1 |

The large `exp`/`sinh`/`cosh`/`pow` errors are relative errors near the top of the range, exactly the
relative error DESIGN.md predicts for `fixPow` and the `exp2` scaling. `fixLog2` is correctly rounded on every
sample; `fixSqrt` is 0 or 1 raw low (toward zero); `fixAtan2` is within 2 raw.

3. **`sqrt`'s seed: Glenn decides before the lock.** `fixSqrt` seeds its integer square root with the IEEE
   double `sqrt` and repairs it; `fixISqrt128High` is the integer-only path and produces identical bits by
   construction (`docs/layer4/moo-l4-math-sqrt.md`: 2^20 inputs, 0 mismatches, 7.30 vs 7.48 ns). This card
   changes neither. The question for Glenn is exactly: *does layer 4 keep `fixSqrt` with its double seed,
   written down as deterministic because IEEE `sqrt` is correctly rounded on every supported platform, or
   does it switch to `fixISqrt128High` to keep the VM double-free even inside a helper?* Measurement gives no
   performance reason either way.
4. **DETERMINISM.md 7 is fixed.** `tests/determinism/math.luau` exercises every math function and `^` over
   domain edges, inexact small fractions, large magnitudes and the named-error cases; its hashes are pinned in
   `tests/determinism/expected.txt`. clang (the gate) and gcc print the same pair; the arm64 and Windows
   nightly columns are `pending nightly` at land, never an unseen value. DETERMINISM.md 7 "Fix:" is to say:

   > The math library is rebuilt on `fixed` (layer 4): `sin`, `cos`, `atan2`, `sqrt`, `exp2`, `log2` from
   > `fixed`, with the rest built from them in integer arithmetic; `^` is exact integer exponentiation for an
   > integer-valued exponent and `fixPow` otherwise. The compiler's folding calls the same fixed functions.
   > `tests/determinism/math.luau` pins the whole library and `^` by effects and state hash on every platform
   > and compiler, and `tests/moo/vectors/math-oracle.bin` pins accuracy without calling libm.

   The status row 7 becomes **fixed** (no libm, no double bridge, no platform libm in folding).

## What changed

- `tests/moo/moo-l4-conformance.test.cpp` (new): `MooL4ConformanceMathTable` decodes the frozen oracle file,
  measures all 16 functions through their DESIGN.md sources against the ideals, prints the table, and asserts
  each measured maximum. It never calls libm.
- `tests/conformance/moo/l3/moo-l4-conformance.luau` (new): run by `MooL3Scripts`; pins the exact points and
  Q48.16 raw values of every function, the saturation range, the named domain errors, and library/fastcall
  agreement.
- `tests/determinism/math.luau` and `tests/determinism/expected.txt` (new program and one new line): the
  determinism suite's math program, with hashes `a005f1cb6d366b18 bb3e5e4465819cce`; arm64/Windows
  `pending nightly`.

## Notes folded: exact document changes for DESIGN.md and DETERMINISM.md

- **math-oracle** (`docs/layer4/moo-l4-math-oracle.md`): the Accuracy oracle paragraph and the "not yet
  measured" sentence, replaced by the table above; "Layers" item 4 says "Checked by: the CORE-MATH error
  table (`tests/moo/moo-l4-math-oracle.test.cpp`, over `tests/moo/vectors/math-oracle.bin`), round-trip of
  every formatted value, and bit-exact agreement with `fixed`."
- **math-sqrt**: remove `sqrt` from the TEMPORARY bridges bullet; record the seed measurement and Glenn's
  decision in "math library"; remove `sqrt` from DETERMINISM.md 7's libm list.
- **math-trig**: remove `sin`, `cos`, `tan` from the TEMPORARY bridges bullet and from DETERMINISM.md 7's
  `lmathlib.cpp`/fastcall lists; the table rows already match.
- **math-inverse-trig**: remove `atan`, `atan2`, `asin`, `acos` from the bridges bullet and from
  DETERMINISM.md 7; keep the named errors `math.asin domain` and `math.acos domain`.
- **math-exp-log**: remove `exp`, `sinh`, `cosh`, `tanh`, `log`, `log10`, `log(x, base)` from the bridges
  bullet and DETERMINISM.md 7; keep the source rows and add the compositions, the base-1 `division by zero`,
  and the measured table rows.
- **math-exact-ops**: remove `fmod`, `modf`, `frexp`, `ldexp`, `deg`, `rad`, `lerp`, `map`, `isnan`,
  `isinf`, `isfinite` and the math constants from the bridge list; add the `deg`/`rad` table row and note
  `fmod(_, 0)` is `division by zero`; DETERMINISM.md 7 drops them.
- **math-noise**: "noise | Luau's Perlin noise, ported to fixed"; "Rebuilt on `fixed`. No libm."; remove
  `math.noise` and `math.lerp` from DETERMINISM.md 7.
- **pow-integer / -vm / -math / -fractional / -fold**: change "it does not fold division by zero, `^`, or any
  builtin call" to "it folds an integer-valued `^` through `moo_pow_int`; it does not fold `0 ^ negative`,
  a fractional `^`, or any builtin call"; the integer bullet's bound becomes the measured sub-raw bound; `^`
  with a fractional exponent is `fixPow`; the status row says no libm and no double bridge; DETERMINISM.md 7
  drops the `luai_numpow`/`ConstantFolding` libm items and says folding uses `moo_pow_int`.
- **format-roundtrip**: no sentence changes (the formatter and parser already hold the design).
- **this card**: "Layers" item 4 becomes the checked-by sentence above; the Accuracy oracle paragraph gains
  the measured table; the unresolved `fixSqrt` paragraph records Glenn's decision or stays open until he
  makes it.
