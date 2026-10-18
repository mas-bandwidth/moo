# moo-l3-vector-compiler-out

The compiler no longer recognises `vector.*` or `CompileOptions::vectorLib` /
`CompileOptions::vectorCtor` as builtins and never folds or emits a vector constant. A call such
as `vector.create(1, 2, 3)` compiles to an ordinary global index (`GETGLOBAL` + `GETTABLEKS` at
`-O0`, `GETIMPORT` at `-O1`/`-O2`) and `CALL`, with no `FASTCALL*` instruction and no
`LBC_CONSTANT_VECTOR` or `LBC_CONSTANT_VECTORD` entry in the constant table.

## Decision

- **Builtin recognition (`Compiler/src/Builtins.cpp`):** `getBuiltinFunctionId` drops both the
  `builtin.object == "vector"` table (`create`, `abs`, `clamp`, `cross`, `dot`, `floor`, `ceil`,
  `magnitude`, `normalize`, `max`, `min`, `sign`, `lerp`) and the `options.vectorCtor` branch
  (which mapped `vectorCtor` — either on `options.vectorLib` or unqualified — to `LBF_VECTOR`).
  Every `vector.*` call and every host-configured `vectorLib.vectorCtor` call returns `-1` (no
  builtin), so the compiler emits neither a `FASTCALL*` instruction nor builtin constant folding.
- **Constant representation (`Compiler/src/ConstantFolding.h`):** `Compile::Constant::Type` drops
  `Type_Vectorf` and `Type_Vectord`, and the `Constant` union drops `valueVectorf[4]` and
  `valueVectord[4]`. A compiler `Constant` can no longer hold a float or double vector.
- **Constant folding (`Compiler/src/ConstantFolding.cpp`, `Compiler/src/BuiltinFolding.cpp`):**
  all `Type_Vectorf` and `Type_Vectord` arms in `constantsEqual`, `vectorsDifferOnlyInW`,
  `foldUnary`, `foldBinary`, `ConstantVisitor::analyze`, `ctype`, `ctypeof` and `foldBuiltin`
  (`LBF_VECTOR`, `cvectorf`, `cvectord`) are removed.
- **Code generation (`Compiler/src/Compiler.cpp`):** `BytecodeCompiler::isConstantVector` always
  returns `false`, `BytecodeCompiler::addConstant` no longer calls `addConstantVectorf` or
  `addConstantVectord` (`grep -c "addConstantVector" Compiler/src/Compiler.cpp` is `0`), and
  `Luau::setCompileConstantVector` / `Luau::setCompileConstantVectord` leave the `CompileConstant`
  at `Type_Unknown` so any host `libraryMemberConstantCb` that still invokes them cannot inject a
  vector constant into the compiler.
- **Out of scope for this card:** `BytecodeBuilder::addConstantVectorf` / `addConstantVectord` and
  `VM/src/lvmload.cpp`'s `LBC_CONSTANT_VECTOR` refusal are handled by `moo-l3-vector-vm-out`.

## What changed

- `Compiler/src/Builtins.cpp`, `getBuiltinFunctionId`: removed the `if (builtin.object == "vector")`
  and `if (options.vectorCtor)` branches.
- `Compiler/src/ConstantFolding.h`, `struct Constant`: removed `Type_Vectorf`, `Type_Vectord`,
  `valueVectorf[4]` and `valueVectord[4]`.
- `Compiler/src/ConstantFolding.cpp`:
  - `constantsEqual`: removed `Type_Vectorf` and `Type_Vectord` cases;
  - `vectorsDifferOnlyInW`: deleted;
  - `foldUnary`: removed `AstExprUnary::Minus` cases for `Type_Vectorf` and `Type_Vectord`;
  - `foldBinary`: removed `Type_Vectorf` and `Type_Vectord` arithmetic and comparison cases;
  - `ConstantVisitor::analyze`: removed `Type_Vectorf` and `Type_Vectord` library constant loading
    and `.x`/`.y`/`.z`/`.w` component extraction.
- `Compiler/src/BuiltinFolding.cpp`:
  - `cvectorf`, `cvectord`: deleted;
  - `ctype`, `ctypeof`: removed `Type_Vectorf` and `Type_Vectord` cases;
  - `foldBuiltin`: removed `case LBF_VECTOR:`.
- `Compiler/src/Compiler.cpp`:
  - `BytecodeCompiler::isConstantVector`: returns `false`;
  - `BytecodeCompiler::addConstant`: removed `Type_Vectorf` (`addConstantVectorf`) and
    `Type_Vectord` (`addConstantVectord`) cases;
  - `Luau::setCompileConstantVector`, `Luau::setCompileConstantVectord`: no-ops (leave constant
    `Type_Unknown`).
- `tests/Compiler.test.cpp`: 11 upstream vector test cases are skipped with
  `// moo: the float vector is removed (DESIGN.md "vec3, quat and transform")`:
  1. `ConstantFoldVectorArith`
  2. `ConstantFoldVectorArith4Wide`
  3. `ConstantFoldVectorCompare`
  4. `ConstantFoldVectorComponents`
  5. `VectorFastCall` (newly skipped)
  6. `VectorFastCall3` (newly skipped)
  7. `VectorConstants`
  8. `VectorConstantFields`
  9. `VectorOptionsDefault`
  10. `VectorOptionsDoubleUnsupported`
  11. `VectorOptionsDouble`
- `tests/moo/moo-l3-vector-compiler-out.test.cpp` (`VectorCompilerOutNoBuiltinOrConstant`,
  `VectorCompilerOutScriptInSandbox`) and
  `tests/conformance/moo/l3/moo-l3-vector-compiler-out.luau`: verify both through the C++
  `BytecodeBuilder::Dump_Code` dump and through `moo_test.compile` in the Luau sandbox that
  `return vector.create(1, 2, 3)` and every `vector.*` library call (as well as `Vector3.new` with
  `CompileOptions::vectorLib` / `vectorCtor` and `setCompileConstantVector` /
  `setCompileConstantVectord`) emit `GETIMPORT` or `GETGLOBAL`, no `FASTCALL*` instruction, and no
  `LBC_CONSTANT_VECTOR` / `LBC_CONSTANT_VECTORD` constant at `-O0`, `-O1` and `-O2`.

## Follow-up outside this card's PATHS

- `tests/MooCompiler.test.cpp:67` (`TEST_CASE("MooBuiltinsDoNotFold")`, in `Luau.UnitTest`) still
  asserts `CHECK(has(mooCompile0("return vector.create(1, 2, 3)"), "FASTCALL"));` from layer 1
  when vector folding was turned off while `vector.create` was still a fastcall builtin. With
  `vector.*` removed from `Compiler/src/Builtins.cpp`, that assertion now sees `GETIMPORT` instead
  of `FASTCALL` and should be updated or dropped when `tests/MooCompiler.test.cpp` is next touched.
- `tests/moo/moo-l5-suite-audit.test.cpp` pins `doctestSkips == 29` and `scripts == 19`; with the
  2 newly skipped `VectorFastCall` / `VectorFastCall3` cases in `tests/Compiler.test.cpp`,
  `doctestSkips` is now 42 (was 40 on `sprint/moo`) and `scripts` is 31 (already 31 on
  `sprint/moo`).

## DESIGN.md sentences that must change

- `docs/DESIGN.md`, "Layer 3 as built", bullet "**The compiler**":

  > - **The compiler** folds number arithmetic with the same `moo_number.h` operations (a constant
  >   still travels as the double that holds it exactly); it does not fold division by zero, `^`,
  >   or any builtin call or `math` constant.

  Add at the end of that bullet:

  > It does not recognise `vector.*` or `CompileOptions::vectorLib` / `vectorCtor` as builtins and
  > never folds or emits a vector constant (`Compiler/src/Builtins.cpp`,
  > `Compiler/src/ConstantFolding.cpp`, `Compiler/src/Compiler.cpp`).

## DETERMINISM.md sentences that must change

- `docs/DETERMINISM.md`, "Status", paragraph 6:

  > Twenty compiler cases that asserted folding moo turned off are skipped in
  > `tests/Compiler.test.cpp`, each pointing at `tests/MooCompiler.test.cpp`, which tests moo's
  > folding.

  Replace with:

  > Eleven compiler cases that asserted `math.*`, `bit32.*` or `^` folding moo turned off are
  > skipped in `tests/Compiler.test.cpp`, each pointing at `tests/MooCompiler.test.cpp`, which
  > tests moo's folding, and eleven compiler cases that asserted float `vector` folding,
  > constants or fastcalls are skipped with `// moo: the float vector is removed (DESIGN.md "vec3, quat and transform")`.

- `docs/DETERMINISM.md`, "17. The `vector` type is float32":

  > - `VM/src/lobject.h:39` stores vector components as `float`; `VM/include/luaconf.h:137`
  >   `LUA_VECTOR_SIZE` is 3. `VM/src/lveclib.cpp` and the compiler's vector folding
  >   (`Compiler/src/ConstantFolding.cpp:371-433`) compute in float.
  >
  > **Fix:** in layer 3 the float vector type is removed. Layer 3b brings `vec3`, `quat` and
  > `transform` back with full Q48.16 components (DESIGN.md, "vec3, quat and transform").

  Add after the "**Fix:**" paragraph:

  > The compiler half is built (`moo-l3-vector-compiler-out`): `Compiler/src/Builtins.cpp` no
  > longer maps `vector.*` or `vectorLib`/`vectorCtor` to a builtin, `Compiler/src/ConstantFolding.*`
  > and `Compiler/src/BuiltinFolding.cpp` have no vector constant or vector fold paths, and
  > `Compiler/src/Compiler.cpp` never calls `addConstantVectorf` or `addConstantVectord`.
