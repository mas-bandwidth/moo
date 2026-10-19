# Named Luau.UnitTest reds after analyzer builtins were removed

`moo-l3-analyzer-builtins` removed `os`, `vector`, and `integer` from the analyzer builtin
definitions (docs/DESIGN.md "Analyzer"). Eleven Luau.UnitTest cases in `tests/TypeInfer.*` and
`tests/TypeFunction.user.test.cpp` assert those removed builtins and are red at BASE. Each is
skipped below with a `moo:` reason on the line above the case naming the design rule; a twelfth
case, `MooBuiltinsDoNotFold` (compiler side), is fixed in `tests/MooCompiler.test.cpp`.

| Case | File | Cause | Disposition | Reason |
|---|---|---|---|---|
| `builtin_table_names` | `tests/TypeInfer.tables.test.cpp` | Expects the `typeof(os)` mutation error; removed `os` is an unknown global | skipped | `os` is not a moo builtin (DESIGN.md "Analyzer") |
| `persistent_sealed_table_is_immutable` | `tests/TypeInfer.tables.test.cpp` | Expects to inspect the sealed `os` table; removed `os` is unknown | skipped | `os` is not a moo builtin (DESIGN.md "Analyzer") |
| `compound_assign_reports_invalid_vector_arithmetic` | `tests/TypeInfer.operators.test.cpp` | Expects an invalid `vector + number` type-function error; `vector` is absent | skipped | `vector` is not a moo builtin (DESIGN.md "Analyzer") |
| `type_narrow_to_vector` | `tests/TypeInfer.refinements.test.cpp` | Expects `unknown & vector`; absent `vector` narrows to `never` | skipped | `vector` is not a moo builtin (DESIGN.md "Analyzer") |
| `type_vector_refine` | `tests/TypeInfer.refinements.test.cpp` | Expects a vector property-refinement error; no vector type remains | skipped | `vector` is not a moo builtin (DESIGN.md "Analyzer") |
| `issubtypeof` | `tests/TypeFunction.user.test.cpp` | Vector references add unknown-symbol diagnostics to the expected type-function errors | skipped | `vector` is not a moo builtin (DESIGN.md "Analyzer") |
| `type_tostring` | `tests/TypeFunction.user.test.cpp` | Its `T<vector>` argument adds an unknown-symbol error to the expected rendering error | skipped | `vector` is not a moo builtin (DESIGN.md "Analyzer") |
| `udtf_cloner_missing_integer_crashes_copy` | `tests/TypeFunction.user.test.cpp` | `integer` is unknown, so the UDTF argument cannot be cloned as the test expects | skipped | `integer` is not a moo builtin (DESIGN.md "Analyzer") |
| `udtf_integer_constructor_is_not_number` | `tests/TypeFunction.user.test.cpp` | `types.integer` is unknown, adding a downstream mismatch | skipped | `integer` is not a moo builtin (DESIGN.md "Analyzer") |
| `udtf_integer_is_distinct_from_number` | `tests/TypeFunction.user.test.cpp` | Both `types.integer` and the `integer` annotation are unknown | skipped | `integer` is not a moo builtin (DESIGN.md "Analyzer") |
| `udtf_integer_methods_work` | `tests/TypeFunction.user.test.cpp` | Both `types.integer` and the `integer` annotation are unknown | skipped | `integer` is not a moo builtin (DESIGN.md "Analyzer") |
| `MooBuiltinsDoNotFold` | `tests/MooCompiler.test.cpp` | Compiler maps removed vector builtins to no fastcall; `vector.create` compiles to `GETIMPORT` | fixed | `vector` is not a moo builtin (DESIGN.md "Analyzer"; docs/layer3/moo-l3-vector-compiler-out.md) |

Replacement coverage for the skipped integer/vector cases lives in
`tests/moo/moo-l3-unittest-base-reds-named.test.cpp` (`Moo.Test`). `Moo.Test` links the VM and the
compiler, not the analyzer, so the case pins the VM side of the seam, matching
`moo-l3-analyzer-builtins.test.cpp`: the removed `os` and `integer` names resolve to no global.

## Gate re-verified on attempt 8

The CARRY head (attempt 7, `c1c6e827`) was rebuilt and re-run with clang 18 (build) and gcc 13
(build-gcc). `build/Luau.UnitTest` prints `5147 passed | 1 failed | 55 skipped`; the one failure is
`fold_constants_div_by_zero` (`tests/BytecodeCallInliner.test.cpp`, waived by the coordinator).
`build/Moo.Test` prints `158 passed | 2 failed`; the two are `MathPowFractionalStaysOnBridge` and
`MooPowIntVmFractionalBridge` (waived). Each of the twelve cases named above shows skipped or passing
alone. The strict gcc build stops in untouched upstream `Analysis/src/BuiltinTypeFunctions.cpp`
(`-Werror=free-nonheap-object`) at the unmodified BASE tip `ad9e85a9` in `build-gcc-base` (exit 1),
so GCC parity is waived; the warning is not suppressed.
