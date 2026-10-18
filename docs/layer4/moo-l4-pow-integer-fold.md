# moo-l4-pow-integer-fold

The compiler's constant folding of `^` with an integer exponent now calls
`moo_pow_int` (`Common/include/moo_pow.h`), the same function the VM and the math library will call, so a
folded constant is bit-identical to the run time's result by construction. The function and its proof are
`moo-l4-pow-integer-design`'s and this card does not change them
(`docs/layer4/moo-l4-pow-integer.md`).

## What changed

- `Compiler/src/ConstantFolding.cpp`
  - `#include "moo_pow.h"` (the header is header-only, so no build registry changes).
  - `mooFold` gains `case AstExprBinary::Pow:`. When the exponent `b` is integer-valued
    (`moo_numisinteger(b)`), it calls `moo_pow_int(a, b >> MOO_NUMBER_FRACTION_BITS, &r)` once:
    - `MOO_POW_OK` folds with `mooResult`;
    - `MOO_POW_SATURATED` folds in release only, through the same `mooResult`. That helper declines a raw
      value a double cannot hold exactly, so a saturated constant (at `INT64_MAX` raw or above) is not
      emitted; the run time's saturating value is unchanged. Under `#if MOO_OVERFLOW_ERRORS` it is not
      folded, because the run time raises `number overflow in '^'`;
    - `MOO_POW_DIVISION_BY_ZERO` is never folded, so `0 ^ negative` still raises `division by zero` at run
      time;
    - a fractional exponent stays unfolded.
  - The `default` comment no longer says pow waits for the math library.
  - The upstream `case AstExprBinary::Pow:` double `pow` is already unreachable for number operands:
    `foldBinary` returns after `mooFold` for two number constants, so it is left as it is.
- NEW `tests/moo/moo-l4-pow-integer-fold.test.cpp`, suite `moo-l4-pow-integer-fold` in `Moo.Test`:
  - `MooFoldPowIntFoldsToPowInt`: `return 3 ^ 4`, `return 2 ^ -3`, `return 1.5 ^ 7` and
    `return 10 ^ 2` compile with no `POWK`, the bytecode dump holds `moo_pow_int`'s raw result, and the
    value the chunk returns is that same raw result. That pins the folded constant to the function rather
    than to a second computation.
  - `MooFoldPowIntLeavesDivisionByZeroAndFractionalUnfolded`: `return 0 ^ -1` and `return 2 ^ 0.5` still
    compile to `POWK`.
- `tests/MooCompiler.test.cpp` is changed: `MooFoldArithIsFixedPoint`'s pin of `10 ^ 2` as `POWK`
  becomes `LOADN R0 100`, because the design note requires that case to change in this card and the file is
  in this card's PATHS. Its header comment likewise notes that `^` with an integer exponent folds through
  `moo_pow_int`. The fold is also pinned in the new test file above.
- NEW this note. `Common/include/moo_pow.h` and `docs/layer4/moo-l4-pow-integer.md` are unchanged.

Not changed: `Compiler/src/BuiltinFolding.cpp`. `foldBuiltin` returns `Constant()` before its switch for
every builtin, so `math.pow` does not fold today; the design note's item 2 ("the same rule through
`moo_pow_int`, or no fold") is met by that "no fold". This card does not reopen builtin folding.

## Decision

The fold calls `moo_pow_int` and no other power. The exponent is `b >> 16`, exact because
`moo_numisinteger` holds, and the status decides folding. This is the design note's rule, so the folded
constant cannot differ from the run time's bits, which is the point of folding fixed-point arithmetic
(`docs/DESIGN.md`, "Layer 3 as built", "The compiler").

## DESIGN.md sentences that must change

"Layer 3 as built", "The compiler" (the exact sentence at BASE):

> - **The compiler** folds number arithmetic with the same `moo_number.h` operations (a constant still travels as the
>   double that holds it exactly); it does not fold division by zero, `^`, or any builtin call or `math` constant.

Replace with:

> - **The compiler** folds number arithmetic with the same `moo_number.h` operations (a constant still travels as the
>   double that holds it exactly); it does not fold division by zero, `0 ^ negative`, `^` with a
>   fractional exponent, any builtin call, or any `math` constant. An integer-valued `^` folds through
>   `moo_pow_int`; `math.pow` remains a builtin call and is not folded.

This states the implemented boundary: only integer-valued `^` folds through `moo_pow_int`; builtin calls,
including `math.pow`, stay runtime calls.

## DETERMINISM.md sentences that must change

No status-table row changes: row 7's `^` wording moves when `moo-l4-pow-integer-vm` and `-math` land, as
`docs/layer4/moo-l4-pow-integer.md` says. One sentence in section 7, "Platform libm in math, `^` and
compile-time folding", is made stale by this card. At BASE it reads:

> - **The compiler folds constants with the build machine's libm**:
>   `Compiler/src/BuiltinFolding.cpp:160-301` and `:595` (every `math.*` call with constant
>   arguments), `Compiler/src/ConstantFolding.cpp:366` (`//`) and `:466` (`^`). So the
>   bytecode itself depends on the platform that compiled it.

The `ConstantFolding.cpp:366` (`//`) and `:466` (`^`) references are stale in the fixed-point tree
(`//` goes through `moo_numidiv`). Integer-valued `^` folds through `moo_pow_int`; fractional `^` and
`0 ^ negative` remain runtime operations. `BuiltinFolding.cpp` returns before its math-folding switch, so
`math.pow` and the other builtin calls remain runtime calls. Replace the compiler bullet with:

> - **The compiler does not fold builtin calls or their libm results**: `Compiler/src/BuiltinFolding.cpp`
>   returns before its `math.*` folding switch, so `math.pow` and other builtin calls remain runtime calls.
>   `Compiler/src/ConstantFolding.cpp` folds `^` only for an integer-valued exponent, through `moo_pow_int`;
>   fractional exponents and `0 ^ negative` stay unfolded. `//` uses `moo_numidiv`. The compiler therefore
>   emits no libm-derived builtin constants, and its folded integer powers are platform-independent.

## Not done

- No VM, `math.pow`, or fastcall path: those are `moo-l4-pow-integer-vm` and `-math`.
- No `BuiltinFolding.cpp` change: builtins do not fold in this tree (above).
- No `docs/DESIGN.md` or `docs/DETERMINISM.md` edit; the layer's closing card folds this note in.
