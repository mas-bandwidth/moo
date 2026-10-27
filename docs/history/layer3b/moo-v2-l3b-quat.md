# moo-v2-l3b-quat

The quat operators and library of layer 3b (`docs/DESIGN.md` "vec3, quat and transform (layer 3b)",
"Operations"), on the quat object of `moo-v2-l3b-quat-object` (the carried sprint head `2f08f543`, never
gated; re-reviewed and carried onto main `f04c4cce` in the same PR). Roadmap row "quat object and operations
(nlerp)".

## Decisions

1. **Operators.** `quat * quat` is fixed's `fixMulQuat` and `quat * vec3` is `fixRotateVector`, in
   `luaV_doarithimpl` (`VM/src/lvmutils.cpp`), ahead of the vec3 arm. Both are fixed's compound functions
   verbatim, as DESIGN.md "Saturation, stated precisely" says of compound functions: `fixMulQuat` reduces each
   component at 128 bits and saturates under `FIX_SATURATE`, so a checked build raises no overflow error for it
   (the named error is for moo's own `+ - *` and unary `-`). Every other operator, and `number * quat`,
   `quat * number`, `vec3 * quat`, `quat + quat`, `-quat`, is the ordinary arithmetic error. Every result is a
   new object.
2. **Components.** `q.x`, `q.y`, `q.z` read the vector part and `q.w` the scalar (fixed's `s`), in either case
   (`X`, `Y`, `Z`, `W`), through `moo_value_index` in `luaV_gettable`, which vec3 shares (a vec3 has no `w`).
   `w` rather than `s` because it is the name Luau and game code use for a quaternion's scalar; `tostring` and
   the C API keep fixed's order x, y, z, s.
3. **`quat.new(x, y, z, w)`.** Omitted components are the identity's, `(0, 0, 0, 1)`, so `quat.new()` is the
   identity. Components are taken as given: no normalization (a script normalizes through `nlerp`, or builds a
   unit quaternion from `math.sin`/`math.cos`).
4. **`quat.nlerp(a, b, alpha)`** is `fixNLerp`. `alpha` outside `[0, 1]` is refused by name
   (`alpha must be in [0, 1]`): fixed asserts the range in `fixVecLerp`, so without the check a checked build
   would abort and a release build would blend outside the arc.
5. **`quat.slerp`** is the named error `quat.slerp waits for fixed's slerp` until `fixed` has a slerp at moo's
   pin (DESIGN.md: it is added upstream first, so fixed3d and moo share one implementation).
6. **C API.** `moo_pushquat(L, x, y, z, s)` and `moo_toquat(L, idx, out[4])` (`VM/include/lualib.h`,
   `VM/src/lapi.cpp`), the vec3 shape. `lua_topointer` returns `NULL` for a vec3 or a quat: two equal values
   are one value, so no address is exposed (DETERMINISM.md 6).
7. **One component hash.** The carried head had a second copy of `moo_vec3_comp_hash` named
   `moo_quat_comp_hash`; `moo_quat_hash` now calls vec3's, so there is one definition. The bits are the same.

## Seams the quat object left open, now closed

The carried quat-object note listed seams outside its paths that "must be added before a quat reaches a
script". All are added here, and the same seams were also missing for vec3 and are added for both:

- `luaV_equalval` and the `LOP_JUMPIFEQ`/`LOP_JUMPIFNOTEQ` switches: `case LUA_TQUAT` compares by value.
  Without it `==` on two quats reached the switch's `LUAU_UNREACHABLE()` in a release build.
- `luaL_tolstring` (`VM/src/laux.cpp`): `quat(x, y, z, s)`.
- `VM/src/lgcdebug.cpp`, `validateobj`, `dumpobj` and `enumobj`: `LUA_TVEC3` and `LUA_TQUAT` are leaves.
  vec3 was missing here too (the vec3 note's card gc item 6), so `luaC_validate`, the heap dump and the heap
  enumeration reached `LUAU_ASSERT(!"unexpected object type")` on any heap with a vec3.
- `lua_topointer`: decision 6 (the vec3 note's card gc item 9 was never done for vec3 either).
- `tests/Determinism.test.cpp`, `hashObject`: a vec3's and a quat's payload is state (the vec3 note's card gc
  item 10, likewise never done). No determinism program makes either, so no pinned hash moved from this.

## What changed

- `VM/src/moo_quatlib.cpp` (new): `luaopen_quat` with `new`, `conjugate`, `nlerp` and `slerp` (refused).
- `VM/src/lvmutils.cpp`: the quat arm of `luaV_doarithimpl`; `moo_value_index` (was `moo_vec3_index`);
  `LUA_TQUAT` in `luaV_equalval`.
- `VM/src/lvmexecute.cpp`: `LUA_TQUAT` in `LOP_JUMPIFEQ` and `LOP_JUMPIFNOTEQ`.
- `VM/src/lapi.cpp`: `moo_pushquat`, `moo_toquat`, and `lua_topointer`'s value cases.
- `VM/src/laux.cpp`, `VM/src/lgcdebug.cpp`, `VM/src/ltable.cpp`, `VM/src/linit.cpp`, `VM/include/lualib.h`,
  `Sources.cmake`, `tests/conformance/types.luau` (`_G.quat` ignored until the analyzer row),
  `tests/Determinism.test.cpp`: as above.
- The quat object (carried): `VM/include/lua.h` (`LUA_TQUAT` 15), `VM/src/moo_quat.h`, `VM/src/lobject.h`
  (`TKey::tt` 5 bits, `next` 27), `VM/src/lstate.h`, `VM/src/lobject.cpp`, `VM/src/lgc.cpp`, `VM/src/lmem.cpp`,
  `VM/src/moo_layout.cpp` (`global_State` 19232 -> 19248), `VM/src/ltm.cpp`; see
  `docs/layer3b/moo-v2-l3b-quat-object.md`.

## Tests

`tests/moo/moo-v2-l3b-quat.test.cpp`, doctest suite `moo-v2-l3b-quat`:

- `QuatMulMatchesFixedFullRange4096`: half full-domain, half unit quaternions against `fixMulQuat`. Reversed
  witness: more than 1000 inputs where `a * b != b * a`, so swapped operands fail.
- `QuatRotateMatchesFixed4096`: unit quaternions (and the identity, which must be exact) and vectors with
  components below 2^24 against `fixRotateVector`. Reversed witness: more than 3000 inputs where the inverse
  rotation differs, so a VM applying the conjugate fails.
- `QuatConjugateMatchesFixedFullRange4096`: full domain against `fixConjugate`.
- `QuatNlerpMatchesFixed4096`: unit quaternions, alpha in `[0, 1]` with the edges 0, 1/2 and 1, against
  `fixNLerp`. Reversed witness: more than 2000 inputs where `1 - alpha` differs.
- `QuatValueBehaviour`: components, `tostring`, constructor defaults, equality, keys, the C API both ways
  (refusal included), `lua_topointer`, and one allocation per operation (48 bytes for a quat result, 40 for a
  vec3).
- `QuatRefusals`: 28 refused operations, each with its exact error, and the alpha edges accepted.
- `QuatConformance`: `tests/conformance/moo/l3/moo-v2-l3b-quat.luau`, the behaviour a script sees (exact
  half-turn rotations, identity, conjugate, nlerp end points and midpoint, slerp refused).

Why rotate is not compared over the full domain: `fixRotateVector` is built from `fixCross` and `fixMulAdd`,
whose sums are plain int64 arithmetic at this `fixed` pin (DESIGN.md "Saturation, stated precisely"). Outside
the tested range the result is undefined in C, and the gap is owed upstream in `fixed` with vec3's cross.

## Re-pins

One cause: the `"quat"` type-name string, the `quat` library table and its four function names are startup
objects, so every creation number shifts, and `global_State` grows by 16 bytes (one tag slot each in `mt[]` and
`ttname[]`), which moves GC pacing. clang 18 and gcc 13 give identical values.

`tests/determinism/expected.txt` (effects, state), old (main `f04c4cce`) -> new:

| program | old | new |
|---|---|---|
| coroutines | `14a9b3cff128eb95` `3b97a8bfb6d7a324` | `c2101c58c9141949` `a3ad70d2a2a436f6` |
| gcchurn | `74af43e489ecf54b` `08a560138169a027` | `b7383413c6b36272` `f8f78138d7b28a73` |
| library | `9e5ba12ad834c77d` `9000ab43846170cf` | `9c77cd6d0d41264c` `f5aab4ca0098fe2c` |
| objectkeys | `863a034d939672ec` `d596ebcfc869f955` | `80a94f70cf379757` `7abd0f05a1425361` |
| coroutines2 | `2ca405a9600143fa` `0e031fe08c2e96bf` | `45af4a6f2428da59` `e6f28947afabc910` |
| errors | `73f268e45fb73545` `8d2b96bb8f979fec` | `f7037948e0d4bea2` `568a4adad73fccd8` |
| math | `b166f5d6481b48ef` `7a977c8aef335485` | `61226f5f25360f4e` `04a505678c06a75e` |

`tests/Moo.test.cpp`:

| case | old | new |
|---|---|---|
| `ObjectKeyOrderIgnoresAddresses` | `27,12,41,17,...,29,47,54` | `17,2,31,7,...,30,57,44` |
| `TostringPrintsCreationNumber` | `table: #391 table: #392` | `table: #401 table: #402` |
| `GcPacingIsAFunctionOfTheProgram` | `12944870832997013475` | `7693531136235502622` |
| `DestructorsRunInCreationOrder` | `16890831982340340772` | `17132068042409053668` |
| `WeakTableOrderIsAFunctionOfTheProgram` | `10528855066574528962` | `1561155198007178439` |

The carried quat-object note's own re-pin table is from its sprint base, before the vec3 library existed; it
was never on main, and the values above replace it.

## DESIGN.md sentences that must change

- "Layer 3 as built" gains: "**quat** is the immutable GC object of layer 3b (`LUA_TQUAT`, 48 bytes);
  `quat * quat` is `fixMulQuat`, `quat * vec3` is `fixRotateVector`, `==` is component equality, `q.x`, `q.y`,
  `q.z`, `q.w` read the components; the `quat` library (`new`, `conjugate`, `nlerp`) calls fixed's functions,
  and `quat.slerp` is refused by name until fixed has one. `moo_pushquat` and `moo_toquat` carry raw Q48.16
  components across the C API."
- "Operations": the `quat.slerp` row stays; add "`quat.nlerp` refuses an alpha outside [0, 1]".

## Not done

- `quat.slerp`: waits for `fixed`.
- `fixRotateVector` and vec3 `cross` outside fixed's defined range: owed upstream in `fixed` as saturating
  adds under `FIX_SATURATE`.
