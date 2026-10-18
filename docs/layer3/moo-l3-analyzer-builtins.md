# The analyzer's builtin definitions drop os, vector and integer

## Decision

The analyzer's definitions describe the VM moo actually ships: no `os`, no `vector` type or
library, and no `integer` type or library. The upstream `FFlag::LuauIntegerType2` and
`FFlag::LuauIntegerLibrary` no longer choose the definitions; the `_NOINTEGER` sources are
always selected, and `GlobalTypes` adds no `integer` binding.

At this base the VM still opens the `vector` library (`VM/src/linit.cpp` has `luaopen_vector`),
even though `docs/DESIGN.md` "Analyzer" says layer 3 removes it. So `tests/conformance/types.luau`
drops the `_G.os` ignore as the card asks and keeps a `_G.vector` ignore with its reason: the VM
removal has not landed here. That line goes away when it does.

## What changed

- `Analysis/src/EmbeddedBuiltinDefinitions.cpp` `getBuiltinDefinitionSource`: no
  `kBuiltinDefinitionOsSrc`, no `kBuiltinDefinitionVectorSrc`, no `kBuiltinDefinitionIntegerSrc`;
  always `kBuiltinDefinitionBufferSrc_NOINTEGER`.
- `Analysis/src/EmbeddedBuiltinDefinitions.cpp` `getTypeFunctionDefinitionSource`: always
  `kBuiltinDefinitionTypeMethodSrc_NOINTEGER` and `kBuiltinDefinitionTypesLibSrc_NOINTEGER`.
- `Analysis/src/BuiltinDefinitions.cpp`: the `vector` metatable setup is deleted; no `vector`
  binding is registered to find.
- `Analysis/src/GlobalTypes.cpp`: no `integer` builtin type binding.
- `tests/conformance/types.luau`: `_G.os` ignore removed; `_G.vector` ignore added (see above).

## Tests

- `tests/moo/moo-l3-analyzer-builtins.test.cpp`: `OsAndIntegerAreNotGlobals`,
  `VectorIsStillAVmGlobal` (VM side of the seam).
- `tests/conformance/moo/l3/moo-l3-analyzer-builtins.luau`: the same globals, returning `OK`.
- `build/moo-analyze` on `local v = vector.create(1,2,3)` prints `Unknown global 'vector'`.
- `Luau.UnitTest` cases that use `os`, `vector` or `integer` are skipped by name with
  `// moo: <os|vector|integer> is not a moo builtin (DESIGN.md "Analyzer")`.

## Sentences that must change when the layer closes

- `docs/DESIGN.md` "Analyzer": "the builtin definitions drop `os`, `vector` and the integer
  library while those are out" is now done for `os` and `integer`; `vector` is dropped from the
  definitions but the VM still exposes the library at this base.
- `docs/DESIGN.md` "Analyzer": "Layer 3b adds `vec3`, `quat` and `transform` as builtin types,
  defined the way Luau already defines `vector`." stays as the plan.
- `docs/DETERMINISM.md` "Status", upstream tests changed: "`tests/conformance/types.luau` checks
  that the VM's globals match the analyzer's builtin definitions. It ignores `_G.os` until the
  analyzer's definitions drop `os` (owed, with `vector` and `integer`, see DESIGN.md "Analyzer")."
  must become: the `_G.os` ignore is gone; `_G.vector` is ignored until the VM removal lands.

## Not done

Skips were needed in `tests/TypeFunction.user.test.cpp` (`udtf_*`, `issubtypeof`,
`type_tostring`), `tests/TypeInfer.operators.test.cpp`
(`compound_assign_reports_invalid_vector_arithmetic`), `tests/TypeInfer.refinements.test.cpp`
(`type_narrow_to_vector`, `type_vector_refine`) and `tests/TypeInfer.tables.test.cpp`
(`builtin_table_names`, `persistent_sealed_table_is_immutable`). Those files are outside this
card's PATHS, so they are reported as PATHS-PROPOSED, not edited; `Luau.UnitTest` therefore still
reports them.
