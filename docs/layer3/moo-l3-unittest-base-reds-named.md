# Named Luau.UnitTest reds after analyzer builtins were removed

The eleven disposition causes below were recorded in the preceding checkpoint at `0ca1115f552eef25ddb231b2833459ddb2479409`, after `moo-l3-analyzer-builtins` removed `os`, `vector`, and `integer` from analyzer builtins. The w5 working base is now `e2581b9f05fadf16eab9975a1133b819a83b68db`; Captain has not released a gate allocation, so single-case output at this exact base is not yet available. These rows are historical cause evidence and are not represented as current-base test output.

| Case | File | Cause in prior checkpoint | Disposition | Reason |
|---|---|---|---|---|
| `builtin_table_names` | `tests/TypeInfer.tables.test.cpp` | Expects the `typeof(os)` mutation error; removed `os` is an unknown global | skipped | `os` is not a moo builtin (`docs/DESIGN.md`, “Analyzer”) |
| `persistent_sealed_table_is_immutable` | `tests/TypeInfer.tables.test.cpp` | Expects to inspect the sealed `os` table; removed `os` is unknown | skipped | `os` is not a moo builtin (`docs/DESIGN.md`, “Analyzer”) |
| `compound_assign_reports_invalid_vector_arithmetic` | `tests/TypeInfer.operators.test.cpp` | Expects an invalid `vector + number` type-function error; `vector` is absent | skipped | `vector` is not a moo builtin (`docs/DESIGN.md`, “Analyzer”) |
| `type_narrow_to_vector` | `tests/TypeInfer.refinements.test.cpp` | Expects `unknown & vector`; absent `vector` narrows to `never` | skipped | `vector` is not a moo builtin (`docs/DESIGN.md`, “Analyzer”) |
| `type_vector_refine` | `tests/TypeInfer.refinements.test.cpp` | Expects a vector property-refinement error; no vector type remains | skipped | `vector` is not a moo builtin (`docs/DESIGN.md`, “Analyzer”) |
| `issubtypeof` | `tests/TypeFunction.user.test.cpp` | Vector references add unknown-symbol diagnostics to the expected three type-function errors | skipped | `vector` is not a moo builtin (`docs/DESIGN.md`, “Analyzer”) |
| `type_tostring` | `tests/TypeFunction.user.test.cpp` | Its `T<vector>` argument adds an unknown-symbol error to the expected rendering error | skipped | `vector` is not a moo builtin (`docs/DESIGN.md`, “Analyzer”) |
| `udtf_cloner_missing_integer_crashes_copy` | `tests/TypeFunction.user.test.cpp` | `integer` is unknown, so the UDTF argument cannot be cloned as the test expects | skipped | `integer` is not a moo builtin (`docs/DESIGN.md`, “Analyzer”) |
| `udtf_integer_constructor_is_not_number` | `tests/TypeFunction.user.test.cpp` | `types.integer` is unknown, adding a downstream mismatch | skipped | `integer` is not a moo builtin (`docs/DESIGN.md`, “Analyzer”) |
| `udtf_integer_is_distinct_from_number` | `tests/TypeFunction.user.test.cpp` | Both `types.integer` and the `integer` annotation are unknown | skipped | `integer` is not a moo builtin (`docs/DESIGN.md`, “Analyzer”) |
| `udtf_integer_methods_work` | `tests/TypeFunction.user.test.cpp` | Both `types.integer` and the `integer` annotation are unknown | skipped | `integer` is not a moo builtin (`docs/DESIGN.md`, “Analyzer”) |

`MooBuiltinsDoNotFold` is the coordinator-added twelfth case, first reported red after `moo-l3-vector-compiler-out`. The compiler source maps removed vector builtins to no fastcall; the `MooBuiltinsDoNotFold` expectation is corrected to `GETIMPORT` with a `moo:` rationale. Exact-base execution remains pending the Captain allocation.

The required exact-base reds, current-head whole `Luau.UnitTest` summary, the named-case reruns, `Moo.Test`, and GCC parity remain pending the Captain allocation. See `REPORT.md` for current evidence and status.
