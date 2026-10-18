# Named Luau.UnitTest failures at the carried base

At BASE `0ca1115f552eef25ddb231b2833459ddb2479409`, clang 18.1.3 runs 13 failed tests. Twelve are covered here; the additional `fold_constants_div_by_zero` failure is outside this card's PATHS and is reported as a base red. Each listed case was run alone at the exact base; its complete output is in the allocated evidence directory.

| Case | File | Base cause | Disposition | Reason |
|---|---|---|---|---|
| `builtin_table_names` | `tests/TypeInfer.tables.test.cpp` | Expected `typeof(os)` table diagnostic; analyzer reports unknown global `os` | skipped | `os` is not a moo builtin (DESIGN.md "Analyzer") |
| `persistent_sealed_table_is_immutable` | `tests/TypeInfer.tables.test.cpp` | Expected `os` table mutation diagnostic; analyzer reports unknown global and cannot require type `os` | skipped | `os` is not a moo builtin (DESIGN.md "Analyzer") |
| `compound_assign_reports_invalid_vector_arithmetic` | `tests/TypeInfer.operators.test.cpp` | `utf0` is absent because removed `vector` type prevents the expected arithmetic diagnostic | skipped | `vector` is not a moo builtin (DESIGN.md "Analyzer") |
| `type_narrow_to_vector` | `tests/TypeInfer.refinements.test.cpp` | Expected `unknown & vector`; actual type is `never` because `vector` is absent | skipped | `vector` is not a moo builtin (DESIGN.md "Analyzer") |
| `type_vector_refine` | `tests/TypeInfer.refinements.test.cpp` | Expected a vector refinement error; analyzer has no `vector` type and returns no such error | skipped | `vector` is not a moo builtin (DESIGN.md "Analyzer") |
| `issubtypeof` | `tests/TypeFunction.user.test.cpp` | Three expected errors became four due to unknown `vector` references | skipped | `vector` is not a moo builtin (DESIGN.md "Analyzer") |
| `type_tostring` | `tests/TypeFunction.user.test.cpp` | Expected one error; extra unknown-symbol error is for removed `vector` | skipped | `vector` is not a moo builtin (DESIGN.md "Analyzer") |
| `udtf_cloner_missing_integer_crashes_copy` | `tests/TypeFunction.user.test.cpp` | `integer` is unknown and its UDTF argument is not serializable | skipped | `integer` is not a moo builtin (DESIGN.md "Analyzer") |
| `udtf_integer_constructor_is_not_number` | `tests/TypeFunction.user.test.cpp` | `types.integer` is unknown; downstream mismatch adds an error | skipped | `integer` is not a moo builtin (DESIGN.md "Analyzer") |
| `udtf_integer_is_distinct_from_number` | `tests/TypeFunction.user.test.cpp` | `types.integer` and `integer` are unknown | skipped | `integer` is not a moo builtin (DESIGN.md "Analyzer") |
| `udtf_integer_methods_work` | `tests/TypeFunction.user.test.cpp` | `types.integer` and `integer` are unknown | skipped | `integer` is not a moo builtin (DESIGN.md "Analyzer") |
| `MooBuiltinsDoNotFold` | `tests/MooCompiler.test.cpp` | The vector compiler builtin was removed; the call compiles as `GETIMPORT`, not `FASTCALL` | fixed | Updated only the obsolete vector opcode expectation; retained other assertions |

Attempt 3 re-verified the twelve dispositions at the merged sprint/moo tip `3234bcbd27bd` (clang, `RelWithDebInfo`, `LUAU_WERROR=ON`): all eleven named cases select as skipped, `MooBuiltinsDoNotFold` runs and passes; the only full-suite red is `fold_constants_div_by_zero` (`tests/BytecodeCallInliner.test.cpp:1093`), which is red at that same base and out of this card's scope.
