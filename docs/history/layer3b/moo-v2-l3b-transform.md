# moo-v2-l3b-transform

> History note: this card is a record of how transform was built. The wrappers it names (`moo_transform_mul`, `moo_transform_point`, `moo_transform_invpoint`) and their `moo_numadd`/`moo_numsub` add are gone: the library now calls `fixMulTransforms`, `fixInvertTransform`, `fixTransformPoint`, `fixInvTransformPoint`, `fixVecAdd` and `fixVecSub` directly, and `fixVecAdd` under `FIX_SATURATE` gives the same bits. The current statement is `docs/DESIGN.md` "vec3, quat and transform".

The transform object, operators and library of layer 3b (`docs/DESIGN.md` "vec3, quat and transform (layer 3b)",
"Operations"). Roadmap row "transform object and operations" (cards `moo-v2-l3b-transform-object` and
`moo-v2-l3b-transform`, done together in one PR, as quat's were). Stacked on the quat PR (#34).

## Decisions

1. **The object** copies the vec3 and quat objects' decisions (`docs/layer3b/moo-v2-l3b-vec3-object.md`): tag
   `LUA_TTRANSFORM` = 16, after `LUA_TQUAT`, so `LUA_TDEADKEY` is 17 and still fits the 5-bit `TKey::tt`;
   `MooTransform` is the 16-byte header plus fixed's `fixTransform` (position `fixVec3 p`, rotation `fixQuat q`,
   56 bytes), **72 bytes** in all; equality by the seven raw components; a table key hashed by the seven raw
   components (`moo_transform_hash`: vec3's and quat's component hashes combined); `tostring`
   `transform(vec3(px, py, pz), quat(qx, qy, qz, qs))`; a GC leaf, never cleared from a weak table; no address
   through `lua_topointer`; its payload is state in the determinism hash.
2. **Operations, and where they differ from fixed.** `transform * transform` is `fixMulTransforms`,
   `transform * vec3` and `transform.point` are `fixTransformPoint`, `transform.inverse` is `fixInvertTransform`,
   `transform.direction` is `fixRotateVector(t.q, v)`, `transform.invpoint` is `fixInvTransformPoint` and
   `transform.invdirection` is `fixInvRotateVector(t.q, v)`. Compose, point and invpoint each make one position
   add or subtract with fixed's `fixVecAdd`/`fixVecSub`, which are plain int64 arithmetic at this pin (DESIGN.md
   "Saturation, stated precisely"): an overflow there is undefined in C, so a script could reach behaviour that
   differs between compilers. moo makes that one add with `moo_numadd`/`moo_numsub`, the rule vec3 `+` and `-`
   already use (`VM/src/moo_transformlib.cpp`, `moo_transform_mul`, `moo_transform_point`,
   `moo_transform_invpoint`). Wherever fixed's add does not overflow the bits are fixed's, and the tests compare
   them with fixed's own functions; where it would overflow, moo saturates. As for quat's compound functions, a
   checked build raises no named error for it. What this buys is stated narrowly: the add itself is defined
   everywhere, but with a non-identity rotation a position near the range ends also reaches `fixRotateVector` or
   `fixInvRotateVector` (compose's input, invpoint's subtract result), whose sums can still overflow, so outside
   fixed's defined range the saturation makes the result determined only for the identity rotation. `inverse` has no add (negation is exact on the domain), so it is
   `fixInvertTransform` verbatim. The rotations inside are fixed's verbatim, with the gap quat already names
   (`fixRotateVector`'s `fixCross`/`fixMulAdd` sums are plain int64 outside fixed's defined range).
3. **Parts.** `t.p` reads the position as a new vec3 and `t.q` the rotation as a new quat, fixed's field names,
   lower case only (`moo_transform_index` in `VM/src/lvmutils.cpp`). Each read allocates one object, with no GC
   step inside the index (the debt is paid at the next check). Any other key is the index error; a transform is
   immutable, so `t.p = v` is refused.
4. **`transform.new(p, q)`.** Omitted (or nil) parts are the identity's: the zero vector and `quat(0, 0, 0, 1)`.
   The rotation is taken as given, with no normalization, as `quat.new` does.
5. **C API.** `moo_pushtransform(L, px, py, pz, qx, qy, qz, qs)` and `moo_totransform(L, idx, out[7])`, the vec3
   shape in fixed's `fixTransform` order.
6. **Not added:** `fixInvMulTransforms` (the relative transform). It is not in DESIGN.md's table; a script writes
   `transform.inverse(a) * b`, which is not bit-identical to it. If a game script needs fixed3d's exact relative
   transform, it is one more library function.

## What changed

- `VM/src/moo_transform.h` (new): `MooTransform`, its layout asserts, and the declarations.
- `VM/src/moo_transformlib.cpp` (new): the four position-saturating operations and `luaopen_transform` with
  `new`, `inverse`, `point`, `direction`, `invpoint`, `invdirection`.
- `VM/include/lua.h` (`LUA_TTRANSFORM`), `VM/include/lualib.h`, `VM/src/lapi.cpp`, `VM/src/laux.cpp`,
  `VM/src/lgc.cpp`, `VM/src/lgcdebug.cpp`, `VM/src/linit.cpp`, `VM/src/lmem.cpp`, `VM/src/lobject.cpp`,
  `VM/src/lobject.h`, `VM/src/lstate.h`, `VM/src/ltable.cpp`, `VM/src/ltm.cpp`, `VM/src/lvmexecute.cpp`,
  `VM/src/lvmutils.cpp`, `VM/src/moo_layout.cpp` (`global_State` 19248 -> 19264), `Sources.cmake`: each seam the
  quat object and operations touched, once more for transform.
- `tests/conformance/types.luau`: `_G.transform` ignored until the analyzer row.
- `tests/Determinism.test.cpp`: a transform's payload in `hashObject`.

## Tests

`tests/moo/moo-v2-l3b-transform.test.cpp`, doctest suite `moo-v2-l3b-transform`:

- `TransformCreateAndLayoutInVm`: tag, 72-byte size, creation numbers, totalbytes, no address.
- `TransformValueBehaviour`: tostring (and the longest one fits its buffer), parts, type, equality, keys, each of
  the seven components separating two values and two keys (reversed witness: a compare or hash that skipped one
  component fails), the C API both ways, `luaC_validate` with a live transform, one allocation per operation
  (72 bytes for a transform result, 40 for a vec3, 48 for `t.q`).
- `TransformGcLeafAndChurn100k`: collected when unreachable, totalbytes back to the baseline after 100000 under
  incremental steps, never cleared from a `k`, `v` or `kv` weak table while held.
- `TransformMulMatchesFixed4096`: rigid transforms against `fixMulTransforms`. Reversed witness: more than 4000
  of 4096 where `a * b != b * a`.
- `TransformPointAndDirectionMatchFixed4096`: `t * v`, `transform.point` against `fixTransformPoint`,
  `transform.direction` against `fixRotateVector`. Reversed witnesses: point differs from direction (the position
  is applied) and from the inverse point.
- `TransformInversePointAndDirectionMatchFixed4096`: `invpoint`, `invdirection` against fixed. Reversed witnesses:
  the conjugate is applied, and the position is.
- `TransformInverseMatchesFixed4096`: against `fixInvertTransform`. Reversed witness: the inverse's position is
  the rotated negation, not the plain one.
- `TransformPositionSaturates`: compose, point and invpoint saturate where fixed's add would overflow; one raw
  unit inside the range the result is `MAX - 1` and `MIN + 1`, fixed's own bits.
- `tests/moo/moo-v2-l3b-gc-stack.test.cpp` (`TransformResultSurvivesStackShrinkInGcStep`): the result is stored
  before the GC check, as the quat PR's fix does for vec3 and quat.
- `TransformRefusals`: 28 refused operations, each with its exact error.
- `TransformConformance`: `tests/conformance/moo/l3/moo-v2-l3b-transform.luau`, the behaviour a script sees
  (exact half-turn frames, identity, inverse round trip, composition order).

Planted errors, run once and reverted: composing with the operands swapped, and `invpoint` rotating forward
instead of inverse, each turned `TransformMulMatchesFixed4096` or
`TransformInversePointAndDirectionMatchFixed4096` and `TransformConformance` red.

Rigid transforms in the corpus have positions below 2^40 raw (2^24 units) and unit rotations, so no int64 sum
inside fixed's functions overflows and fixed's functions are a valid oracle.

## Re-pins

One cause, as for quat: the `"transform"` type-name string, the `transform` library table and its six function
names are startup objects, so every creation number shifts, and `global_State` grows by 16 bytes (one tag slot each
in `mt[]` and `ttname[]`), which moves GC pacing.

`tests/determinism/expected.txt` (effects, state), old (PR #34 head `00086167`) -> new:

| program | old | new |
|---|---|---|
| coroutines | `c2101c58c9141949` `a3ad70d2a2a436f6` | `5cb7f17a10d68cc4` `6fad6a2c9bf8a173` |
| gcchurn | `b7383413c6b36272` `f8f78138d7b28a73` | `971f095d77280529` `99a32dc86877a486` |
| library | `9c77cd6d0d41264c` `f5aab4ca0098fe2c` | `b37b92ae87f532ba` `d8e5f88d20c961f8` |
| objectkeys | `80a94f70cf379757` `7abd0f05a1425361` | `1067f47a944c9471` `673230ee8047eeaa` |
| coroutines2 | `45af4a6f2428da59` `e6f28947afabc910` | `45af4a6f2428da59` `5359668bdc22dc58` |
| errors | `f7037948e0d4bea2` `568a4adad73fccd8` | `f7037948e0d4bea2` `be21c103205a5f40` |
| math | `61226f5f25360f4e` `04a505678c06a75e` | `0d7b989a30435fce` `6f1e734e163b3843` |

`tests/Moo.test.cpp`:

| case | old | new |
|---|---|---|
| `ObjectKeyOrderIgnoresAddresses` | `17,2,31,7,...,30,57,44` | `3,34,60,17,...,26,24,43,30` |
| `TostringPrintsCreationNumber` | `table: #401 table: #402` | `table: #415 table: #416` |
| `GcPacingIsAFunctionOfTheProgram` | `7693531136235502622` | `109304034735195062` |
| `DestructorsRunInCreationOrder` | `17132068042409053668` | `12678846546305343080` |
| `WeakTableOrderIsAFunctionOfTheProgram` | `1561155198007178439` | `14301579112442529628` |

`tests/moo/moo-v2-l3b-vec3-object-design.test.cpp`: `LUA_TTRANSFORM` follows `LUA_TQUAT`, `LUA_TDEADKEY` and
`LUA_T_COUNT` are 17.

## DESIGN.md sentences that must change

- "Layer 3 as built" gains: "**transform** is the immutable GC object of layer 3b (`LUA_TTRANSFORM`, 72 bytes);
  `transform * transform` composes and `transform * vec3` transforms a point, `==` is component equality, `t.p` and
  `t.q` read the parts; the `transform` library (`new`, `inverse`, `point`, `direction`, `invpoint`,
  `invdirection`) is fixed's functions with the one position add saturating."
- "Representations costed", item (a): "Sizes are 32 bytes for vec3, 40 for quat and 64 for transform with the GC
  header" becomes "40 bytes for vec3, 48 for quat and 72 for transform": moo's header carries the 8-byte creation
  number, so each object is 16 bytes of header plus its payload.
- "Saturation, stated precisely": add "transform's compose, point and invpoint make their one position add with
  moo's saturating add, so they equal fixMulTransforms, fixTransformPoint and fixInvTransformPoint wherever those
  are defined."

## Not done

- `fixInvMulTransforms` as a library function (decision 6).
- The analyzer's `transform` type: the analyzer row.
- The rotations outside fixed's defined range: owed upstream in `fixed`, as for quat.
