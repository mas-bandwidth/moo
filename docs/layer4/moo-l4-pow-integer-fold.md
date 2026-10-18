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
    `return 10 ^ 2` compile with no `POWK`, and the value the chunk returns is `moo_pow_int`'s raw
    result. That pins the folded constant to the function rather than to a second computation.
  - `MooFoldPowIntLeavesDivisionByZeroAndFractionalUnfolded`: `return 0 ^ -1` and `return 2 ^ 0.5` still
    compile to `POWK`.
- `tests/MooCompiler.test.cpp` is not changed. The design note says `MooFoldArithIsFixedPoint`'s pin of
  `10 ^ 2` as `POWK` changes in this card, but that file is outside this card's PATHS, so the card cannot
  edit it; the `10 ^ 2` fold is instead pinned in the new test file above. `MooCompiler.test.cpp:58`'s
  `POWK` assertion is now stale and fails once this fold lands.
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
>   fractional exponent, or any other builtin call or `math` constant; `^` and `math.pow` with an integer
>   exponent fold through `moo_pow_int`.

(The `math.pow` phrase is the design note's wording; `BuiltinFolding.cpp` still folds no builtin at all,
so in this tree `math.pow` stays unfolded and the phrase describes the fold path if builtin folding
returns.)

## DETERMINISM.md sentences that must change

No status-table row changes: row 7's `^` wording moves when `moo-l4-pow-integer-vm` and `-math` land, as
`docs/layer4/moo-l4-pow-integer.md` says. One sentence in section 7, "Platform libm in math, `^` and
compile-time folding", is made stale by this card. At BASE it reads:

> - **The compiler folds constants with the build machine's libm**:
>   `Compiler/src/BuiltinFolding.cpp:160-301` and `:595` (every `math.*` call with constant
>   arguments), `Compiler/src/ConstantFolding.cpp:366` (`//`) and `:466` (`^`). So the
>   bytecode itself depends on the platform that compiled it.

The `ConstantFolding.cpp:366` (`//`) and `:466` (`^`) references are already stale in the fixed-point tree
(`//` goes through `moo_numidiv`), and `^` now has no libm fold at all: an integer exponent folds through
`moo_pow_int`, a fractional exponent does not fold. Replace the compiler bullet with:

> - **The compiler folds constants with the build machine's libm**:
>   `Compiler/src/BuiltinFolding.cpp:160-301` and `:595` (every `math.*` call with constant
>   arguments, today disabled by its early return). `^` with an integer exponent folds through
>   `moo_pow_int`; `^` with a fractional exponent does not fold. So the bytecode itself depends on
>   the platform that compiled it for the builtin calls.

## Not done

- No VM, `math.pow`, or fastcall path: those are `moo-l4-pow-integer-vm` and `-math`.
- No `BuiltinFolding.cpp` change: builtins do not fold in this tree (above).
- No `docs/DESIGN.md` or `docs/DETERMINISM.md` edit; the layer's closing card folds this note in.
