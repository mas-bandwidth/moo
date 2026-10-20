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

## Re-pins

Opening a library allocates its table and function-name strings, so every creation number shifts. Per
the precedent of moo-v2-l3b-vec3-object-design, `tests/determinism/expected.txt` and five frozen values
in `tests/Moo.test.cpp` are re-pinned once; clang and gcc produce the same new values.

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
- `tests/conformance/moo/l3/moo-v2-l3b-vec3-ops.luau`: one `assert` per operator and library function,
  a release/checked saturation edge, and `return "OK"`.

## Not done

- `tostring(vec3)` still prints `vec3: #<seq>`: `moo_vec3_tostring` is not wired into `luaL_tolstring`
  (`VM/src/laux.cpp`, outside this card's PATHS). The conformance script reads components through
  `vec3.dot` with a basis vector instead.
- Quat and transform operators, and the analyzer's vec3 type, are later cards.
