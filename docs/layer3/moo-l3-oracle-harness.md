# moo-l3-oracle-harness: every number operator, bit-exact against fixed's C

Layer 3 is checked by "bit-exact agreement with `fixed`'s C on every operator over random and edge operands"
(DESIGN.md "Layers", 3). Before this card, `tests/MooNumber.test.cpp` checked `moo_number.h` against hand-chosen
cases and its own two-word arithmetic (`tests/MooWide.h`), never against fixed and never through the VM or the
compiler. This card adds the oracle. It changes no moo code: `moo_number.h`, the VM and the compiler are as they were,
and the harness found no mismatch.

## What changed

- `tests/moo/moo_oracle_fixed.c` (new): the oracle. It is fixed's own C, compiled as C in its own translation unit
  with `FIX_SATURATE` (it refuses to build without it), and called from C++ through `extern "C"`. `moo_oracle_mul`
  is `fixMul` and `moo_oracle_div` is `fixDiv`. For the operations moo defines on top of fixed, the file writes a
  second definition straight from DESIGN.md's "Semantics" table. These are `moo_oracle_add`, `_sub`, `_unm`, `_idiv`,
  `_mod` and the six comparisons, written in fixed's 128-bit arithmetic (`fixInt128`, `fixed_int128.h`): each result
  is computed exactly in 128 bits and then saturated (`mooOracleSaturate`), and floor division and remainder come
  from one exact floor quotient (`mooOracleFloorQuotient`). None of it calls `moo_number.h`, whose definitions are
  64-bit range checks. moo code still never spells `__int128`: `fixInt128` is fixed's type, native on gcc and clang
  and emulated on MSVC.
- `tests/moo/moo-l3-oracle-harness.test.cpp` (new): doctest suite `moo-oracle`, built into `Moo.Test` by the
  layout's glob.
  - `MooOracleEveryOperator` runs all 13 operators: `+ - * / // %`, unary `-`, `< <= > >= == ~=`. It computes each
    one five ways and checks every way against the oracle, bit for bit (`MooOracleHarness::compare`):
    - **direct**: the `moo_number.h` function;
    - **vm**: the VM on register operands. A script `R[i] = A[i] OP B[i]` reads operand tables the host filled
      through the C API, so nothing folds;
    - **fold**: the compiler, on `(lit) OP (lit)` with both operands written as exact decimal literals;
    - **k-right** and **k-left**: `A[i] OP (lit)` and `(lit) OP B[i]`, which reach the K-operand opcodes (`ADDK`,
      `SUBRK`, `DIVRK` and the rest).

    For `/`, `//` and `%` with a zero divisor, the oracle has no answer: its precondition excludes that case. There,
    each path runs alone (`checkZero`) and must raise `division by zero`. The direct path is not run, because
    `moo_number.h` does not define division by zero.
  - `MooOracleNegativeControlCatchesOneUnit` plants a one-raw-unit error in `*` on the moo side: one unit too many on
    every fixMul rounding tie. The test requires the harness to go red, print the mismatch, and stop at the tenth.
    The same pairs with the real `*` must agree.
  - `MooOracleLiteralsAndGenerator` pins the harness's own pieces. The literal text it writes reads back to the exact
    raw value, and its PCG32 gives the reference generator's first outputs (seed 42, stream 54).
- `tests/conformance/moo/l3/moo-l3-oracle-harness.luau` (new): checks the same rules from inside a script, run by
  `MooL3Scripts`. It covers: folded result equals run-time result for each operator; ties of `*` round half up; `-0`
  is `0`; saturation at both ends; the signs of `//` and `%`; division by zero (folded and not) raises the named error.

## Operands

- **The edge set**, crossed with itself (53 values, 2809 pairs). It holds 0, every magnitude below, and each one's
  negation:
  - 1, 2 and 3 raw;
  - 0x7fff, 0.5 and 0.5 + 1 raw;
  - 0xffff (every fraction bit set), 1, 1 + 1 raw, 1.5 and 2 - 1 raw;
  - 2^31 and 2^32 raw;
  - about 2^39.5 raw, where the square saturates `fixMul`;
  - 2^40 raw;
  - 2^47 - 1, 2^47 and 2^47 + 1 raw (the edge of `fixDiv`'s 64-bit fast path);
  - 2^48 raw;
  - 2^53 - 1 and 2^53 raw (the edge of an exact literal);
  - the largest integer `MOO_NUMBER_INT_MAX << 16` and one unit below it (the edge of `//` saturation);
  - `MOO_NUMBER_MAX - 1`, `MOO_NUMBER_MAX - 1.0` and `MOO_NUMBER_MAX` itself (INT64_MAX raw).

  The crossing includes the ties of `fixMul`, where the dropped bits are exactly half: an odd raw times 0.5, or 1.5
  times one raw.
- **Random pairs**: `MOO_ORACLE_N` of them (default 2^16), from PCG32 with a fixed seed (`0x6d6f6f2d6f72636c`, stream 3),
  so every run checks the same pairs. Each operand is drawn one of four ways, with equal odds: anywhere in the whole
  domain; below 2^24 raw; any bit width from 1 to 63; or an edge value moved by up to 8 raw units, saturated into the
  domain.

## Output

On a mismatch the harness prints one line. The line gives the operator, both operands as raw hex and decimal, the
oracle's value and every path's value. The harness stops after 10 mismatches. A run prints three summary lines, for
example at the default N:

```
moo-oracle pairs=68345 edge=2809 random=65536 seed=0x6d6f6f2d6f72636c zero-divisors=3198 literal-skipped=481693
moo-oracle paths direct=885287 vm=888485 fold=406792 k-right=362475 k-left=361380
moo-oracle checked=888485 operators=13 mismatches=0
```

`checked` counts operator-and-pair cases, and each case can run up to five paths. `MooOracleEveryOperator` takes
3.2 s at the default N (amd-1, clang 21, RelWithDebInfo). The time comes from doctest's own `--duration=true`, not
from a clock in the test, because unit tests use no real time. The nightly runs it as
`MOO_ORACLE_N=16777216 build/Moo.Test -ts=moo-oracle`.

## Decisions

1. **The literal paths skip operands of 2^53 raw and above.** Until the moo bytecode version lands, a literal travels
   as the double that holds it (DESIGN.md "Layer 3 as built", TEMPORARY bridges), and a double holds a raw value
   exactly only below 2^53. Above that, the literal in the source and the constant the VM gets are different
   numbers, so a mismatch there would be the known bridge and not an operator fault. The harness counts the skipped
   cases (`literal-skipped`), and the direct and vm paths still check every pair. A folded result of 2^53 or above
   is not folded (`mooResult` in Compiler/src/ConstantFolding.cpp); it runs in the VM on the exact constants and is
   still checked. **Owed to the bytecode version card:** when constants carry the raw value, delete
   `mooOracleLiteralExact` and its two callers, so the literal paths take every operand.
2. **A k path, beside fold.** Luau folds constants at every optimization level. An expression with two literal
   operands therefore never reaches the K-operand opcodes, and the opcodes with one constant operand (`ADDK`,
   `SUBRK`, `DIVRK`, `IDIVK`, `MODK`) are separate VM code from the register opcodes. The literal batch checks them
   too, for the cost of compiling two more expressions per pair.
3. **The negative control lives in the suite.** It is a reversed witness that runs on every push, so a harness that
   stopped comparing (for example, after a refactor) fails the push gate. The DONE-WHEN throwaway (making
   `moo_numadd` wrap) was also run once by hand. It printed 10 `op=+` mismatches, then the edit was reverted.

## What DESIGN.md and DETERMINISM.md must say (for moo-l3-conformance)

- DESIGN.md "Layer 3 as built": add a bullet after "**Exact already:**":

  > - **Checked against fixed's C** (`tests/moo/moo-l3-oracle-harness.test.cpp`, doctest suite `moo-oracle`, oracle
  >   `tests/moo/moo_oracle_fixed.c`): every operator, bit for bit, on `moo_number.h`, the VM, the compiler's folding and
  >   the K-operand opcodes, over the edge set crossed with itself and 2^16 seeded random pairs (`MOO_ORACLE_N` raises
  >   it for the nightly). Until the bytecode version, the literal paths skip operands of 2^53 raw and above.

- DESIGN.md "Layers", item 3: the sentence "Checked by: bit-exact agreement with `fixed`'s C on every operator over
  random and edge operands." becomes "Checked by: bit-exact agreement with `fixed`'s C on every operator over random
  and edge operands (the `moo-oracle` suite, below "Conformance (layer 3)")."
- DESIGN.md: a new section "### Conformance (layer 3)" before "### Conformance (layer 3b)":

  > Bit-exact against `fixed`'s own C at the pin, built with `FIX_SATURATE` as moo builds it: `fixMul` and `fixDiv` for
  > `*` and `/`, and for `+ - // %`, unary `-` and the comparisons a second definition from the table in "Semantics",
  > written in fixed's 128-bit arithmetic (`tests/moo/moo_oracle_fixed.c`). Every operator runs four ways: on
  > `moo_number.h`, in the VM on operands passed through the C API, folded by the compiler from literals, and in the
  > K-operand opcodes. All four must give the oracle's bits, and `/ // %` by zero must raise `division by zero` on
  > every path. The operands are an edge set crossed with itself: zero, one raw unit, 0.5, 1, every fraction bit, the
  > ties of `fixMul`, 2^31, 2^32, 2^47 and 2^48 raw, the saturation edges of `*` and `//`, and the ends of the range.
  > To these are added PCG32 pairs with a fixed seed, 2^16 on every push and 2^24 in the nightly. A planted one-unit
  > error in `*` must turn the suite red (`MooOracleNegativeControlCatchesOneUnit`).

- DETERMINISM.md "Status", row 7: "**partly**: `+ - * / // %` are fixed's integer arithmetic, and the compiler folds
  with the same functions;" becomes "**partly**: `+ - * / // %` are fixed's integer arithmetic, and the compiler folds
  with the same functions, checked bit-exact against fixed's C on every path by the `moo-oracle` suite;" and the rest
  of the row is unchanged.
