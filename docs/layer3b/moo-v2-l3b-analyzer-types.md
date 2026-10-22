# moo-v2-l3b-analyzer-types

vec3, quat and transform as analyzer builtin types (`docs/DESIGN.md` "Analyzer": "Layer 3b adds `vec3`, `quat` and
`transform` as builtin types, defined the way Luau already defines `vector`"). Roadmap row "vec3, quat and transform
as analyzer builtin types". Stacked on the transform PR (#35).

## Decisions

1. **Extern types, as upstream's vector.** `Analysis/src/EmbeddedBuiltinDefinitions.cpp`,
   `kBuiltinDefinitionMooValueTypesSrc`, appended by `getBuiltinDefinitionSource`, declares `vec3`, `quat` and
   `transform` with `declare extern type ... with`, read-only components, and the operators as metamethods, exactly
   those the VM's `luaV_doarithimpl` implements:
   - `vec3`: `x y z` (and `X Y Z`); `__add`, `__sub` with a vec3; `__mul` with a vec3 or a number; `__unm`.
     `number * vec3` type-checks through the same `__mul(number)` overload, as in the VM.
   - `quat`: `x y z w` (and upper case); `__mul` with a quat (a quat) or a vec3 (a vec3).
   - `transform`: `p` (vec3) and `q` (quat); `__mul` with a transform (a transform) or a vec3 (a vec3).
2. **Libraries with the VM's signatures.** `vec3`: `new`/`create (x?, y?, z?)`, `dot`, `cross`, `length`/`magnitude`,
   `normalize`. `quat`: `new (x?, y?, z?, w?)`, `conjugate`, `nlerp (a, b, alpha)`. `transform`: `new (p?, q?)`,
   `inverse`, `point`, `direction`, `invpoint`, `invdirection`. All `@checked`.
3. **`quat.slerp` is not declared.** The VM keeps it as the named error `quat.slerp waits for fixed's slerp`
   (`docs/layer3b/moo-v2-l3b-quat.md`, decision 5), so the analyzer reports `Key 'slerp' not found` instead of
   promising a quat. `tests/conformance/types.luau` ignores `_G.quat.slerp` with that reason; `_G.vec3`, `_G.quat`
   and `_G.transform` are no longer ignored, so every other function is checked to exist on both sides.

## Tests

- `tests/MooAnalyzer.test.cpp` (Luau.UnitTest, suite `MooAnalyzerValueTypes`):
  - `moo_value_types_accept_everything_the_vm_implements`: a strict script using every component, operator and
    function, each result annotated with the type the VM produces, has no errors.
  - `moo_value_types_report_what_the_vm_refuses`: 15 cases, each a type error with its message: `vec3 + quat`,
    `vec3 * quat`, `vec3 / vec3`, `vec3 + number`, `quat + quat`, `quat * number`, `transform * quat`,
    `transform + transform`, `quat.slerp`, `a.w` on a vec3, `t.x` on a transform, wrong argument types, a missing
    argument.
- `tests/conformance/moo/l3/moo-v2-l3b-analyzer-types.luau` (Moo.Test): the same program runs in the VM and each
  value has the type the analyzer gave it.
- `tests/conformance/types.luau` (Luau.Conformance `Types`). Planted witness, run once and reverted: deleting
  `invdirection` from the definitions turned it red (`_G.transform.invdirection missing from type information`).

## DESIGN.md sentences that must change

- "Analyzer": "Layer 3b adds `vec3`, `quat` and `transform` as builtin types, defined the way Luau already defines
  `vector`" becomes "... are builtin extern types with their operators as metamethods; `quat.slerp` is not declared
  while the VM refuses it."

## Not done

- No new analyzer warning (for example on a quat built with non-unit components); none is specified.
