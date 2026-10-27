# moo-v2-l3b-vec3-ops

The vec3 operators and library of layer 3b (docs/DESIGN.md "vec3, quat and transform (layer 3b)",
"Operations").

## Decision

None beyond the design. The operators are the slow-path arithmetic path only: `luaV_doarithimpl` in
`VM/src/lvmutils.cpp` applies fixed's functions, and `lvmexecute.cpp` keeps its number fast paths and
sends a vec3 to the slow path. Equality is by value in both `luaV_equalval` and the two equality
jumps (`LOP_JUMPIFEQ`, `LOP_JUMPIFNOTEQ`), never by address. A vec3 with any other operand falls
through to the metamethod path and raises the ordinary arithmetic error.

`+`, `-` and unary `-` follow number's rule: `+` and `-` are `moo_numadd`/`moo_numsub` per component
(saturating; `moo_numaddsaturates`/`moo_numsubsaturates` raise `number overflow in '<op>'` in a
checked build), because fixed's `fixVecAdd`/`fixVecSub` are plain int64 adds at this pin. `*` is
fixed's `fixVecMul` and `fixMulSV` (fixed's `fixMul`, saturating under `FIX_SATURATE`).
`-vec3` is fixed's `fixVecNeg`.

## What changed

- `VM/src/lvmutils.cpp`: `mooVec3Payload`, `mooVec3Add`, `mooVec3Sub`, the saturation tests and
  `mooVec3SetResult`; the vec3 arm of `luaV_doarithimpl`; `LUA_TVEC3` in `luaV_equalval`.
- `VM/src/lvmexecute.cpp`: `LUA_TVEC3` in the `LOP_JUMPIFEQ` and `LOP_JUMPIFNOTEQ` switches; a
  `moo:` note in the ADD/SUB/MUL/UNM slow paths.
- `VM/src/moo_vec3lib.cpp` (new): `luaopen_vec3` with `new`, `dot`, `cross`, `length`, `normalize`,
  and the Luau aliases `create` and `magnitude`. Every function calls fixed's `fixDot`, `fixCross`,
  `fixLength` or `fixNormalize`.
- `VM/src/linit.cpp`: the `vec3` entry in both library tables, so `luaL_openlibs` opens it.
- `VM/include/lualib.h`: `luaopen_vec3`, and the C API `moo_pushvec3`/`moo_tovec3` (raw components).
- `VM/src/lapi.cpp`: `moo_pushvec3` (one allocation, `luaC_checkGC`) and `moo_tovec3` (returns 0 when
  the value is not a vec3).
- `Sources.cmake`: `moo_vec3lib.cpp`. The Makefile's `VM/src/*.cpp` wildcard already picks it up.
- `VM/src/ltm.cpp` needed no change: `typeof` already returns `vec3` from the tag's type name.
- `tests/conformance/types.luau`: `_G.vec3` joins the ignore list, as `_G.vector` already does, because
  the analyzer does not know the `vec3` global until the analyzer-type card lands; without this the
  VM-globals-versus-RTTI check fails now that `luaL_openlibs` opens `vec3`.

## Decisions added on review (2026-10-21)

The carried head (`7dcc4e5b`, never gated) was re-reviewed on main `934c2e5`. What changed:

- **Components.** `v.x`, `v.y`, `v.z` (and `X`, `Y`, `Z`, as Luau's vector) read a component, in
  `luaV_gettable` (`moo_vec3_index`, `VM/src/lvmutils.cpp`), the path every non-table index takes.
  Any other key is the ordinary index error; assigning a component is the index error too, because a
  vec3 is immutable and has no metatable. DESIGN.md says the analyzer defines `vec3` "the way Luau
  already defines `vector`", and that definition has `x`, `y` and `z`; without them a script could
  read a component only through `vec3.dot` with a basis vector.
- **tostring.** `luaL_tolstring` (`VM/src/laux.cpp`) prints `vec3(x, y, z)` through
  `moo_vec3_tostring`, as decision 5 of `moo-v2-l3b-vec3-object.md` and its card gc item 7 asked;
  before this, `tostring` printed `vec3: #<seq>`.
- **Checked-build test bug.** The carried `Vec3SaturationEdges` required an overflow error for an
  add that lands exactly on the domain end (`max - 1 + 1`), which is exact and raises nothing; the
  case failed in every checked build. Each edge now says whether it saturates, and `*` (vec3 and
  scalar, both orders) joined the edges.
- **Carried allocation check.** It read `lua_pushvalue(L, -2)` below the stack after `lua_pcall`
  (an assert crash in a checked build) and compared with `lua_rawequal`, which is by value for a vec3
  and so could not see a new object. It now checks that one `+` adds exactly `sizeof(MooVec3)` (40)
  to `lua_totalbytes` with the collector stopped.
- **Constant folder.** Layer 3 removed upstream's vector cases from `foldBinary`, so `"a" + 1` or
  `true < 2` reached `LUAU_ASSERT(!"Unexpected binary operation")` and a checked build crashed while
  compiling (`tests/determinism/errors.luau` has such an expression). Those operators now leave the
  expression unfolded for the VM's error (`tests/moo/moo-l3-fold-nonnumber-operand.test.cpp`).

## Re-pins

Opening a library allocates its table and function-name strings, so every creation number shifts. Per
the precedent of moo-v2-l3b-vec3-object-design, `tests/determinism/expected.txt` and five frozen values
in `tests/Moo.test.cpp` are re-pinned once; clang and gcc produce the same new values.
Re-checked on main `934c2e5` on hulk (x86_64): clang 18 and gcc 13 give the same new values, and no
other value moved. Nothing in the re-review (components, `tostring`, the folder) creates a startup
object, so the values are the carried head's.

`tests/determinism/expected.txt` (effects, state):

| program | old | new |
|---|---|---|
| coroutines | `87415de4bd8d788a` `5c5b83003f192d3f` | `14a9b3cff128eb95` `3b97a8bfb6d7a324` |
| gcchurn | `457e563d872baf1e` `2abd24e2bddab39e` | `74af43e489ecf54b` `08a560138169a027` |
| library | `4d053eec9658fe53` `1e2a00317a2595ce` | `9e5ba12ad834c77d` `9000ab43846170cf` |
| objectkeys | `5e3e1deb0bac6e7b` `507a9f51a8f19397` | `863a034d939672ec` `d596ebcfc869f955` |
| coroutines2 | `46858120d5307229` `f66cdf65627402aa` | `2ca405a9600143fa` `0e031fe08c2e96bf` |
| errors | `f7037948e0d4bea2` `a8553ceb065dad36` | `73f268e45fb73545` `8d2b96bb8f979fec` |
| math | `2f1dd357cba511b4` `dda4446980e68248` | `b166f5d6481b48ef` `7a977c8aef335485` |

`tests/Moo.test.cpp`:

| case | old | new |
|---|---|---|
| `TostringPrintsCreationNumber` | `table: #376 table: #377` | `table: #391 table: #392` |
| `GcPacingIsAFunctionOfTheProgram` | `1918608859321230699` | `12944870832997013475` |
| `DestructorsRunInCreationOrder` | `4673712535580941492` | `16890831982340340772` |
| `WeakTableOrderIsAFunctionOfTheProgram` | `11785329807427589751` | `10528855066574528962` |
| `ObjectKeyOrderIgnoresAddresses` | `42,27,41,56,8,...,20,2` | `27,12,41,17,16,...,13,29,47,54` |

## DESIGN.md sentences that must change

- "Layer 3 as built", bullet "TEMPORARY bridges" is unchanged. Add to "The number is fixed" or a new
  bullet: "**vec3** is the immutable GC object of layer 3b, and its `+`, `-`, `*` and unary `-` follow
  the number's saturation and checked-build error; `vec3 == vec3` is component equality; the `vec3`
  library (`new`, `dot`, `cross`, `length`, `normalize`, aliases `create` and `magnitude`) calls fixed's
  functions directly and is opened by `luaL_openlibs`. `moo_pushvec3` and `moo_tovec3` carry raw Q48.16
  components across the C API (lualib.h)."
- In "vec3, quat and transform (layer 3b)", "Operations" stays as written; no sentence is deleted. The
  representation table's note that each operation allocates a new object is now implemented.

## DETERMINISM.md sentences that must change

- "Status": the startup object set now includes the `vec3` library table and its function names, so
  every creation number in `tests/determinism/expected.txt` is re-pinned; the mechanism (creation
  number, not address) is unchanged.

## Tests

- `tests/moo/moo-v2-l3b-vec3-ops.test.cpp`: 1024 seeded inputs per operator against `fixVecAdd`,
  `fixVecSub`, `fixVecMul`, `fixMulSV`, `fixVecNeg`; 1024 for `==`; 1024 per library function against
  `fixDot`, `fixCross`, `fixLength`, `fixNormalize`; allocation; saturation edges; the conformance file.
- `Vec3OperatorsFullRange4096` and `Vec3LibraryFullRange4096`: 4096 seeded full-domain inputs (random
  magnitudes plus the edges 0, +-1 raw, +-1, +-2^47 raw, the domain ends and their neighbours) against
  `moo_numadd`/`moo_numsub`/`fixVecMul`/`fixMulSV`/`fixVecNeg` and `fixDot`/`fixLength`/`fixNormalize`;
  a checked build must raise exactly where a component saturates. Reversed witnesses: the corpus
  saturates each saturating operator more than 100 times, makes `fixDot` saturate more than 100 times,
  and reaches more than 100 inputs where fixed's plain int64 add would wrap, so a corpus too small to
  reach the edges fails. `cross` is compared only where fixCross's subtractions stay in the domain
  (more than 1000 cases): outside it fixCross is the open gap of DESIGN.md "Saturation, stated
  precisely", owed upstream in `fixed`.
- `Vec3ComponentsAndTostring` and `Vec3Refusals`: components, `tostring`, the raw C API both ways,
  and 29 refused operations each with its exact error (`vec3 + 1`, `vec3 / vec3`, `<`, `#`, `..`,
  `v.w`, `v.x = 1`, calls, iteration, every library function's argument check).
- `tests/conformance/moo/l3/moo-v2-l3b-vec3-ops.luau`: one `assert` per operator and library function,
  components and `tostring`, a release/checked saturation edge, and `return "OK"`.

## Not done

- `fixCross` outside the domain (a plain int64 subtraction at this `fixed` pin) is owed upstream in
  `fixed` as saturating adds under `FIX_SATURATE` (DESIGN.md "Saturation, stated precisely").
- Quat and transform operators, and the analyzer's vec3 type, are later cards.
