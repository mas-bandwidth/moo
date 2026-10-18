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

A node built directly by a tool from a double (the AST JSON encoder tests do this) keeps the two-argument
constructor, which takes the boundary rule's raw value; the parser uses the four-argument form with the exact raw
value, so no conversion happens on the program path.

## Integer literals are refused

At `Parser.cpp` `parseNumber`, with `FFlag::LuauIntegerType2` on, a token ending in `i` is now the parse error
`moo has no integer type`, before any `AstExprConstantInteger` is built. `parseInteger64` and the integer branch are
deleted; Luau's integer type is out (DESIGN.md "integer").

## What changed

- `Ast/include/Luau/Ast.h`, `Ast/src/Ast.cpp`: `AstExprConstantNumber::rawValue`; the four-argument constructor;
  the two-argument constructor applies the boundary rule for tools that build a node from a double.
- `Ast/src/Parser.cpp`: `parseDouble` returns the raw value as well as the double; `parseNumber` passes it to the
  AST; the "Until the moo bytecode version" sentence is deleted; the integer-literal branch is the named error.
- `Compiler/src/ConstantFolding.cpp` `analyze` and `Compiler/src/Compiler.cpp` `compileExpr`: the literal constant
  is `expr->rawValue`, with no `moo_numfromdouble`.
- Three upstream integer-type cases are skipped with a `moo:` reason because the literal they parse is now an
  error: `IntegerBcb` (`tests/Compiler.test.cpp`) and `negated_integer_literal_is_a_constant` and
  `negating_a_non_literal_integer_is_an_error` (`tests/TypeInfer.operators.test.cpp`). Those two files are outside
  this card's PATH list; the skip is required for no newly failing Luau.UnitTest case.

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
- DETERMINISM.md: no sentence changes. No frozen hash or `tests/determinism/expected.txt` changes; the literal is
  now the value the run time already computed for the same decimal.
