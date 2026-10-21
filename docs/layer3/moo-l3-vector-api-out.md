# moo-l3-vector-api-out: no vector in the C API

## Decision

The float vector is now refused at every surface layer 3 names: the library is out
(`moo-l3-libs-out`), the bytecode constant is refused (`moo-l3-vector-constant-refused`), and this
card removes the C API. A host that calls `lua_pushvector`, `lua_tovector`, `lua_isvector`,
`luaL_checkvector` or `luaL_optvector` fails to compile, because the declarations are gone rather
than stubbed. `LUA_TVECTOR` stays in the tag enum, unused, for the same reason DESIGN.md
"integer" keeps `LUA_TINTEGER`: upstream merges stay mechanical. The type itself leaves in a later
layer 3 card.

The conformance host still runs the upstream userdata cases that pass vectors around. Since the
public C API is gone, `tests/Conformance.test.cpp` builds those test vectors through the VM's
internal `TValue` seam (`lapi.h`, `setvvalue`, `luaA_toobject`), not through a second public path.
Those helpers are test-only and marked `moo:`. `CLI/src/Repl.cpp` needed no change: its vector
printing was `luaL_tolstring`/the vector library, and neither can receive a vector now.

## What changed

- `VM/include/lua.h`: removed `lua_tovector`, `lua_pushvector` (both width arms) and the
  `lua_isvector` macro. `LUA_TVECTOR` in the tag enum is untouched.
- `VM/include/lualib.h`: removed `luaL_checkvector` and `luaL_optvector` (the second had no
  callers once the first was gone).
- `VM/src/lapi.cpp`: removed `lua_tovector` and `lua_pushvector`; no other body used them.
- `VM/src/laux.cpp`: removed `luaL_checkvector`, `luaL_optvector`, and the now-unreachable
  `LUA_TVECTOR` case of `luaL_tolstring`.
- `tests/Conformance.test.cpp`: replaced the `lua_pushvector3` macro and every
  `lua_pushvector`/`luaL_checkvector` call with the test-only `mooTestPushVector`,
  `mooTestPushVector3` and `mooTestCheckVector`; `setupVectorHelpers` still installs the test-only
  `vector.create` the upstream scripts call.
- New `tests/moo/moo-l3-vector-api-out.test.cpp`: `OpenlibsLeavesNoVectorInGlobals` and
  `SandboxLeavesNoVectorInGlobals` scan the globals table (and one table level down) for
  `LUA_TVECTOR` and check `vector` is nil.
- New `tests/conformance/moo/l3/moo-l3-vector-api-out.luau`: no script-visible path makes a vector.

## Sentences the closing card must fold in

`docs/DESIGN.md`, "Layer 3 as built", TEMPORARY bridges bullet, currently:

> the libm functions of `math`, `^`, `math.noise`, and the float `vector` type convert through doubles
> (`moo_numbridgetodouble`, `moo_numbridgefromdouble`), until the math library is rebuilt on fixed and the vector
> type is removed;

the `vector` half is now unreachable (no library, constant or C API can make one); drop it so the
bullet reads "the libm functions of `math`, `^`, `math.noise` ... until the math library is rebuilt
on fixed".

`docs/DESIGN.md`, "vec3, quat and transform", currently:

> Luau's `vector` is three float32 components packed into the `TValue`. moo removes it in layer 3
> (the library, the type and the bytecode constant, each refused by name).

add the C API to that list: "(the library, the type, the bytecode constant and the C API, each
refused by name)".

`docs/DETERMINISM.md` section 17, "The `vector` type is float32", Fix bullet, currently:

> **Fix:** in layer 3 the float vector type is removed. Layer 3b brings `vec3`, `quat` and
> `transform` back with full Q48.16 components (DESIGN.md, "vec3, quat and transform").

append: "The library, the bytecode constant and the C API (`lua_pushvector`, `lua_tovector`,
`lua_isvector`, `luaL_checkvector`) are removed first; `LUA_TVECTOR` stays unused in the tag enum
for upstream merges."

`docs/DETERMINISM.md`, "Status", the moved-script list currently names "(basic, buffers, math,
tables, errors, events, bitwise, tpack, debug, strconv, iter, vector, vector_library)"; `libs-out`
deleted `vector_library.luau` and this card leaves only `vector.luau` (the remaining Vector case),
so drop `vector_library` from that list.

## Not changed

`VM/src/lveclib.cpp` still calls the removed names, but `Sources.cmake` and the `Makefile` already
leave it out of the build (`moo-l3-libs-out` kept the file in the tree unbuilt). `VM/src/lvector.cpp`
still implements the GC vector object and `LUA_TVECTOR` still exists; removing them is a later
layer 3 card.
