# moo-l4-math-oracle: the CORE-MATH accuracy oracle for every math function

Layer 4 measures every math function's maximum error in raw units against CORE-MATH (DESIGN.md "math library",
"Accuracy oracle"). This card builds that oracle as an offline generator and a frozen vector file, so moo's tests never
need CORE-MATH or libm. It also adds a `Moo.Test` case that prints the error table. It changes no moo code: the VM,
the compiler and `moo_number.h` are as they were.

## Decisions

1. **What is measured is the `fixed` source of each function, called directly.** The brief asks for the error of
   "each function moo already implements on fixed". At this BASE that set is empty: every libm function of `math`
   still crosses the libm bridge (DESIGN.md "Layer 3 as built", TEMPORARY bridges; `VM/src/lmathlib.cpp`). Measuring
   `math.sin` through the VM today would measure glibc, not moo, and would call libm from a test. The test therefore
   measures the `fixed` function that DESIGN.md's "math library" table names as each function's source, at the pin
   moo vendors:
   - `fixSin` and `fixCos` for sin and cos;
   - `fixAtan2` for atan2;
   - `fixSqrt` for sqrt;
   - `fixLog2` for log2 (the table's base of `log`);
   - `fixPow` for pow.

   `fixPow` is measured on base > 0 only. The table's integer-exponent rule (exact binary exponentiation) is not
   built, and `fixPow` returns 0 for a base <= 0.

   The other functions (tan, asin, acos, atan, exp, log, log10, sinh, cosh, tanh) are built from these in a way
   layer 4 has not written yet, so they are listed as "not measured". Their ideal answers are in the file already,
   so the card that builds each one adds one row to `mooOracleSources` and its line appears.
2. **No bound is asserted.** As the brief says, the bounds are layer 4's decision, after measurement. The test
   asserts only that the file decodes, that its hash matches, and that every count matches its header. It also
   asserts that a damaged file is refused, and that the file's ideal at the points where the real answer is exact
   (sin 0, cos 0, exp 0, log 1, sqrt 1, asin 1 = π/2, and so on) is what the definition gives.
3. **The sample is smaller than the brief's, to fit 8 MB.** The brief asks for an exhaustive pass where the domain
   is at most 2^24 raw values, and 2^20 draws elsewhere. That is about 120 MB at 6 to 14 bytes a record. The file
   instead holds:
   - asin and acos exhaustive, 131073 inputs each;
   - for every other function, 65536 seeded draws plus the edges, a quarter of them in the function's dense core;
   - exp, sinh, cosh and tanh sampled in their live range rather than exhaustive (10 to 30 MB each otherwise).

   The file is 5,814,611 bytes. tools/moo-math-oracle/README.md, "Size", says how; `MOO_ORACLE_SAMPLES` changes the
   draw count.
4. **Inputs are exact doubles.** The ideal is defined as the correctly rounded double of f(x). That needs x itself
   to be exact as a double, so every input has at most 53 significant bits. Raw values at and above 2^53 are taken
   on the double grid (`mooOracleExact`); the domain limit `INT64_MAX` raw is sampled as `2^63 - 2^10` raw.
5. **A result beyond the range saturates.** The boundary rule refuses out-of-range doubles, and moo's arithmetic
   saturates (DESIGN.md "Overflow"), so the ideal of, for example, exp(40) or tan near π/2 is `INT64_MAX` raw (or
   `-INT64_MAX`).
6. **sqrt's ideal is the C library's `sqrt`.** CORE-MATH has no `sqrt`, because IEEE 754 makes `sqrt` itself
   correctly rounded. The generator is built with `-fno-builtin -fno-fast-math -ffp-contract=off`.
7. **Not wired into the nightly.** Wiring the nightly is the coordinator's follow-up. The step: on a change to
   `tools/moo-math-oracle/` or to the CORE-MATH pin, a Linux job runs the README's regenerate command and
   `cmp tests/moo/vectors/math-oracle.bin` against the committed file.

## What changed

- `tools/moo-math-oracle/gen.c` (new): the generator, standalone C that includes nothing of moo's.
  - `mooOracleFromDouble` is moo's boundary rule, copied from `moo_numfromdouble` (Common/include/moo_number.h).
  - `mooOracleIdeal` is the DESIGN.md "Accuracy oracle" definition, with saturation.
  - `mooOracleExact` puts inputs on the exact-double grid, and `mooHalfPiMultiple` gives the nearest raw value of
    k·π/2.
  - The sample of each function is defined by `functions[]`, `mooDraw` and `mooEdges`. The file is written by the
    `main` loop, as varints with an FNV-1a trailer.
- `tools/moo-math-oracle/CMakeLists.txt` (new): a standalone project, never part of moo's build. It pins CORE-MATH
  at `dae5dd8c0d5bd2691b45de28c9edbe12f067c3d3` by the SHA-256 of each of the 21 files it compiles, and refuses any
  other tree. It builds the 15 CORE-MATH functions
  with `-O2 -fno-fast-math -ffp-contract=off`.
- `tools/moo-math-oracle/README.md` (new): how to regenerate the file, why it is frozen, the ideal, the samples, the
  size and the format.
- `tests/moo/vectors/math-oracle.bin` (new, 5,814,611 bytes): 16 functions, 960,926 records. Its header names the
  CORE-MATH commit and the sample definition.
- `tests/moo/moo-l4-math-oracle.test.cpp` (new): doctest suite `moo-l4-math-oracle`, built into `Moo.Test`.
  - `MooL4MathOracleVectorsDecode` reads the file (`MooOracleReader`), checks the trailer, each count and the 16
    names, and checks that asin and acos cover [-1, 1] exhaustively.
  - `MooL4MathOracleVectorsRefuseDamage` is a reversed witness: flipping a byte at five places, and miscounting a
    block with the trailer recomputed, must each be refused.
  - `MooL4MathOracleExactPoints` checks the ideal at the exact points.
  - `MooL4MathOracleErrorTable` prints two lines for each measured function. The first covers every record. The
    second covers the dense core only: |x| <= 64, or for pow a base <= 16 and |exponent| <= 8. Each line gives the
    maximum error in raw units with its input, and the count of inputs at each error: exact for |e| <= 4, then by
    powers of two.
- `tests/conformance/moo/l3/moo-l4-math-oracle.luau` (new): the same exact points through the VM, run by
  `MooL3Scripts`. At those points the libm bridge and the oracle agree.

## Measured (hulk, clang 18.1.3, RelWithDebInfo; gcc 13.3.0 prints the same 22 lines byte for byte)

| function | source | all records: max error (raw) | core: max error (raw) | core: share exact |
|---|---|---|---|---|
| sin | fixSin | 131066 (large arguments) | 108 | 886 of 21111 |
| cos | fixCos | 131068 (large arguments) | 108 | 1873 of 21001 |
| atan2 | fixAtan2 | 2 | 2 | 11781 of 15297 |
| sqrt | fixSqrt | 1 (toward zero) | 1 | 10038 of 20177 |
| log2 | fixLog2 | 0 | 0 | all 19914 |
| pow (base > 0) | fixPow | about 2.6e14 at results near 2^47 | about 1.1e14 | 28255 of 32073 exact over all |
| tan, asin, acos, atan, exp, log, log10, sinh, cosh, tanh | none yet | not measured | | |

Findings for layer 4 to decide on (this card fixes none of them: they are in `fixed`, upstream):

- **`fixSin` and `fixCos` are off by up to 108 raw units (about 0.0016) inside [-64, 64]**, and their errors spread
  evenly between -128 and +128 raw. That is the error of the approximation `fixComputeCosSin` uses (extern/fixed/src/fixed_math.c:105, a
  rational form `(pi^2 - 4y^2) / (pi^2 + y^2)`, Bhaskara I's), not a fault of the oracle.
- **For large arguments, `fixSin` and `fixCos` are wrong by up to the whole range (2.0).** `fixUnwindAngle` reduces
  by `FIX(6.28318530718)`, which is 2π rounded to a raw value (411775 raw, 0.15 raw above 2π). That error grows
  with the number of turns. At 7151966246150948864 raw (about 2^62.6), `fixSin` gives -65534 raw where the ideal is
  +65532.
- **`fixAtan2` is within 2 raw units** on every sample, consistent with fixed's "about 0.0023 degrees" (2.6 raw).
- **`fixSqrt` rounds toward zero**, as documented: half the samples are 1 raw low, and none are higher.
- **`fixLog2` was correctly rounded on all 47044 samples.**
- **`fixPow`'s error is relative, as DESIGN.md predicts.** It is exact at small results and grows with the result.
- DESIGN.md says `fixSqrt` "seeds its integer square root with the IEEE double square root". At the vendored pin,
  `fixSqrt` is `fixISqrt128High`, which uses integer arithmetic only (extern/fixed/include/fixed/fixed.h). The
  question that paragraph leaves to Glenn may already be settled; moo-l4-conformance should check before folding.

## What DESIGN.md and DETERMINISM.md must say (for moo-l4-conformance)

- DESIGN.md "math library", the paragraph **Accuracy oracle**: after "...and publishes the table." add:

  > The oracle is `tests/moo/vectors/math-oracle.bin`, generated offline by `tools/moo-math-oracle` from CORE-MATH
  > at a pinned commit and frozen. It holds asin and acos exhaustively over [-1, 1], and 65536 seeded samples plus
  > edges for every other function. `tests/moo/moo-l4-math-oracle.test.cpp` prints each function's error table.
  > Measured at the fixed pin: `fixLog2` exact on every sample; `fixSqrt` within 1 raw unit, toward zero;
  > `fixAtan2` within 2 raw units; `fixSin` and `fixCos` within 108 raw units for |x| <= 64, and unbounded for large
  > arguments (the reduction by a rounded 2π).

  Then "Known today from `fixed`'s own documentation: `sqrt` is exact (error below one raw unit, toward zero);
  `atan2` is within about 0.0023 degrees (about 2.6 raw units). The other bounds are not yet measured and are not
  claimed." becomes "The bounds of the functions built on these are measured when each is built, and are not claimed
  before then."
- DESIGN.md "math library", the paragraph that begins "`fixSqrt` seeds its integer square root with the IEEE double
  square root": check it against `extern/fixed/include/fixed/fixed.h`, where `fixSqrt` at the pin is
  `fixISqrt128High`, integer only (see Findings).
- DESIGN.md "Layers", item 4: "Checked by: the CORE-MATH error table, round-trip of every formatted value, and
  bit-exact agreement with `fixed`." becomes "Checked by: the CORE-MATH error table
  (`tests/moo/moo-l4-math-oracle.test.cpp`, over `tests/moo/vectors/math-oracle.bin`), round-trip of every formatted
  value, and bit-exact agreement with `fixed`."
- DESIGN.md "Layer 3 as built": no sentence changes (this card changes no built behaviour).
- DETERMINISM.md "7. Platform libm in math, `^` and compile-time folding", **Fix:** after "See DESIGN.md, "math
  library"." add: "Their accuracy is measured against CORE-MATH's correctly rounded results, frozen in
  `tests/moo/vectors/math-oracle.bin`, so the check itself never calls libm."
