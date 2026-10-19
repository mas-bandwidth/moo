# moo-l3-ast-raw-literals: the AST carries the exact raw number literal

Card `moo-l3-ast-raw-literals`. It removes the last double from the literal path. Before this card the parser read a
decimal literal exactly with `moo_numparse`, then threw the raw Q48.16 value away into a `double`; the compiler
converted that double back with `moo_numfromdouble` (the boundary rule). A double holds a raw value exactly only
below 2^53, so a literal above 2^37 rounded twice: `140737488355327.99998474121094` (the largest value) compiled to
0. Now `AstExprConstantNumber` carries the raw value the parser read, and the compiler emits it verbatim.

## Decision: the AST keeps both the raw value and the nearest double

`AstExprConstantNumber` gains `fixed_t rawValue`, set by `parseDouble` from `moo_numparse`. `double value` stays, as
the nearest double to the raw value, because the analyzer and tools read it and (DESIGN.md "Analyzer") never depend
on the representation. The compiler reads `rawValue` in both places it reads a literal: `ConstantFolding.cpp`
`analyze` and `Compiler.cpp` `compileExpr`. Both `moo_numfromdouble` calls and their `moo:` comments are gone.

## Decision: two constructors, two invariants

The two constructor forms keep different invariants, and `Ast.h` states both at the constructors:

- The four-argument form, which the parser uses: `rawValue` is `moo_numparse`'s exact raw value and `value` is
  `moo_numbridgetodouble(rawValue)`, the nearest double to it. A parsed `8.2` holds raw 537395 and the double
  537395 / 65536 = 8.1999969482421875. This is the only form on the program path, so no conversion happens there.
- The two-argument form, for a tool that builds a node from a double: `value` is the tool's double as given and
  `rawValue` is `moo_numbridgefromdouble(value)`, the nearest raw value to it, saturating, NaN to 0. A tool-built
  `8.2` keeps the double 8.2 and has raw 537395.

Reason: no program path builds a node from a double (the only callers of the two-argument form are the upstream AST
JSON encoder tests, which pass 8.2, 0.1677721600000003, infinity and NaN and pin the double the encoder prints, for
example `8.1999999999999993` and `Infinity`). Rewriting `value` to the nearest double to the raw value would change
what those tests and any outside tool read back, would turn infinity into 2^47 and NaN into 0, and would need
`tests/AstJsonEncoder.test.cpp`, which this card does not own. The raw value, the one the compiler emits, is the
nearest Q48.16 value to the tool's double in both readings. `MooAstConstantNumberFormsKeepTheirInvariant` pins both
invariants (pass 5, after a reader found the earlier header comment claimed the parsed-node invariant for every
node).

## Integer literals are refused

At `Parser.cpp` `parseNumber`, with `FFlag::LuauIntegerType2` on, a token ending in `i` is now the parse error
`moo has no integer type`, before any `AstExprConstantInteger` is built. `parseInteger64` and the integer branch are
deleted; Luau's integer type is out (DESIGN.md "integer").

## What changed

- `Ast/include/Luau/Ast.h`, `Ast/src/Ast.cpp`: `AstExprConstantNumber::rawValue`; the four-argument constructor;
  the two-argument constructor applies the saturating bridge for tools that build a node from a double.
- `Ast/src/Parser.cpp`: `parseDouble` returns the raw value as well as the double; `parseNumber` passes it to the
  AST; the "Until the moo bytecode version" sentence is deleted; the integer-literal branch is the named error.
- `Compiler/src/ConstantFolding.cpp` `analyze` and `Compiler/src/Compiler.cpp` `compileExpr`: the literal constant
  is `expr->rawValue`, with no `moo_numfromdouble`.
- Three upstream integer-type cases are skipped in place because they test Luau integer-literal semantics, which
  moo now rejects by name: `IntegerBcb` (`tests/Compiler.test.cpp`) and `negated_integer_literal_is_a_constant` and
  `negating_a_non_literal_integer_is_an_error` (`tests/TypeInfer.operators.test.cpp`). Both test files are in this
  attempt's PATHS, and each skip's `moo:` reason sits immediately above its case for the suite audit.

## Sentences that must change in docs/DESIGN.md "Layer 3 as built" and docs/DETERMINISM.md

- DESIGN.md, "Layer 3 as built", "The compiler": "- **The compiler** folds number arithmetic with the same
  `moo_number.h` operations (a constant still travels as the double that holds it exactly); it does not fold
  division by zero, `^`, or any builtin call or `math` constant." Change the parenthetical to "a constant is the raw
  Q48.16 value".
- DESIGN.md, "Layer 3 as built", "Literals are exact": "Until the bytecode version PR, the AST and the bytecode
  carry the double that holds the value, which is exact below 2^37." Change to "The AST and the bytecode carry the
  exact raw Q48.16 value, over the whole range."
- DESIGN.md, "integer": after "The type tag stays in the C API, unused, so upstream merges in that area stay
  mechanical.", add "An integer literal (`1i`, `0x10i`) is the parse error `moo has no integer type`, and no
  `AstExprConstantInteger` is built."
- `Common/include/moo_number.h`, the comment above `moo_numbridgefromdouble` (outside this card's paths, so not
  edited here): "Only the math library and the float vector type call it, and both go in later layer 3 PRs." is stale,
  since the AST's two-argument `AstExprConstantNumber` constructor now calls it too. Change it to "Only the math
  library, the float vector type and the AST's two-argument `AstExprConstantNumber` constructor (for a tool that
  builds a node from a double; docs/layer3/moo-l3-ast-raw-literals.md) call it; the first two go in later layer 3
  PRs."
- DETERMINISM.md: no sentence changes. No frozen hash or `tests/determinism/expected.txt` changes; the literal is
  now the value the run time already computed for the same decimal.

## Gate at pass 5

Head `57e1fb4f` (pass 5's commit `99629856` with sprint/moo `e7ac1b4e` merged in; the eleven base commits touch no
file of this card) on amd-3, and the unchanged BASE `e7ac1b4e` on vision, both with clang, every DONE-WHEN command.
The new cases and script are green. Two suites carry failures that already exist at BASE, so the all-green DONE-WHEN
line is not met by this card alone; no failure at head is new:

- `tools/revendor-fixed.sh --check`: `extern/fixed matches pin 2f1ed913374a -- clean`, at BASE and head.
- `tools/moo-check-no-clock.sh build/libLuau.VM.a`: `references no clock`, at BASE and head.
- `git grep -c 'Until the moo bytecode version' Ast/src/Parser.cpp`: no match at either (the sentence is wrapped
  over two lines at BASE); `git grep -c 'Until the moo' Ast/src/Parser.cpp` is 1 at BASE and no match at head.
- `build/Luau.Conformance` and `build/Luau.CLI.Test`: SUCCESS at BASE and head.
- `build/Moo.Test`: BASE 155 cases, 3 fail (43 assertions); head 159 cases, 2 fail (42 assertions). The failing case
  names at head (`MathPowFractionalStaysOnBridge`, `MooPowIntCertainEverywhere`, `MooPowIntFrozenHash`,
  `MooPowIntVmFractionalBridge`, from the moo-l4 pow tests) are a subset of BASE's; BASE also fails
  `MooL5SkippedCasesNameAReason`, which passes at head (45 skips, each with its `moo:` reason).
- `build/Luau.UnitTest`: BASE 5162 cases, 13 fail (14 assertions); head 5159 cases (the three integer-literal cases
  skipped), the same 13 fail (14 assertions).
- `moo-l3-ast-raw-literals` suite at head: 4 of 4 cases, 55 assertions.

Red at BASE: with the new C++ file added, Moo.Test does not compile (`no member named 'rawValue' in
'Luau::AstExprConstantNumber'`); with only the new script, `MooL3Scripts` fails with
`moo-l3-ast-raw-literals.luau: error: moo:5: the largest literal`. Planted at head: the compiler reading the double
through the bridge (`moo_numbridgefromdouble(expr->value)` in `ConstantFolding.cpp` and `Compiler.cpp`) turns
`MooLiteralRawAboveFiftyThreeBits` red on `70368744177663.00001525878906`; the tool form normalising `value` to the
raw value's double turns `MooAstConstantNumberFormsKeepTheirInvariant` red on 8.2, infinity and NaN. Both reverted.
