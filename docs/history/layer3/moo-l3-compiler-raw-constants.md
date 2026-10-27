# moo-l3-compiler-raw-constants: the compiler folds and emits raw Q48.16 constants

Card `moo-l3-compiler-raw-constants`. It removes the last double from the compiler's number path. Before this card,
a folded constant travelled as the double that held its value exactly: the compiler rounded each operand by the
boundary rule when it read it, refused to fold any result at or above 2^53 raw, and emitted every number constant
through `BytecodeBuilder::addConstantNumber(double)`. Now a `Compile::Constant` number is the raw Q48.16 value, and
the compiler reads and emits it raw, with no `2^53` limit.

## Decision 1: Constant::Type_Number's value is the raw Q48.16 fixed_t

**Choice.** `Constant::valueNumber` (ConstantFolding.h) is `fixed_t`, the raw Q48.16 value, not `double`. It is the
smallest diff: the field keeps its name and the union keeps one number member. `fixed_t` is `int64_t`
(`extern/fixed/fixed.h`), so the union layout is unchanged and the bytecode builder's raw constant (`int64_t`) is
the same type.

**Why.** The number's one definition is `moo_number.h` (DESIGN.md "Representation"). A folded constant must be the
same bits the run time computes, at every magnitude. A double can hold a raw value only below 2^53, so the old double
path had to refuse to fold above that, silently changing behavior for large sums. Storing the raw value removes both
the limit and the round-trip. The bytecode already carries raw constants (card `moo-l3-bytecode-version`), so the
compiler emits them verbatim.

## What changed

- `Compiler/src/ConstantFolding.h`: `Constant::valueNumber` is `fixed_t`; includes `moo_number.h`.
- `Compiler/src/ConstantFolding.cpp`:
  - `mooOperands` reads the two operands raw (no boundary-rule conversion).
  - `mooResult` folds every in-range result; the `2^53` rejection is gone.
  - `foldUnary` negates with `moo_numunm` and `#` with `moo_numfromint`.
  - `constantsEqual` compares raw values.
  - `foldBinary`'s number arithmetic goes only through `mooFold`; the upstream `double` fallback cases for `+ - * / // % ^` and the comparisons were dead and are removed.
  - `analyze` converts an `AstExprConstantNumber` double once with `moo_numfromdouble` (the boundary rule), marked for card `moo-l3-ast-raw-literals` to remove.
- `Compiler/src/Compiler.cpp`: every number constant emission calls `BytecodeBuilder::addConstantNumberRaw`. The `LOADN` short encoding now tests `moo_numisinteger`/`moo_numtrunc`; the `AstExprConstantNumber` literal converts once with `moo_numfromdouble`; the bit32.extract and table-index integer checks read the raw value.
- `Compiler/src/BuiltinFolding.cpp`: the folded `double`/`bit32` bodies are gone; `foldBuiltin` and `foldBuiltinMath` keep only the seam that answers "not a constant" (the builtins stay unfolded, `MooBuiltinsDoNotFold`).
- `Compiler/src/CostModel.cpp`: `getNumber` reads the raw value and converts by the fixed->double bridge (`moo_numbridgetodouble`), exact for the small integers the loop trip count cares about.

## Decision 2: where the AST still hands the compiler a double

The AST's `AstExprConstantNumber::value` is still a `double`. It is converted once, by the boundary rule
(`moo_numfromdouble`), at the two places the compiler reads it (ConstantFolding.cpp `analyze`, Compiler.cpp
`compileExpr`). Both carry a `moo:` comment that card `moo-l3-ast-raw-literals` removes when the AST carries the raw
value.

## Sentences that must change in docs/DESIGN.md "Layer 3 as built" and docs/DETERMINISM.md

- DESIGN.md, "Layer 3 as built", "The compiler" (currently: "a constant still travels as the double that holds it
  exactly"): change to "a constant is the raw Q48.16 value, and a folded constant is the exact value the same
  expression computes at run time, at every magnitude". The TEMPORARY-bridges bullet "bytecode carries a double per
  number constant" is already gone with card `moo-l3-bytecode-version`.
- DESIGN.md, "Literals are exact", "Until the bytecode version PR, the AST and the bytecode carry the double that
  holds the value, which is exact below 2^37": the bytecode half is gone; the AST half stays until card
  `moo-l3-ast-raw-literals`.
- DETERMINISM.md: no hash or expectation changes; this card only changes how the compiler stores what it already
  folds. The folded/run-time agreement is unchanged and now holds at every magnitude.
