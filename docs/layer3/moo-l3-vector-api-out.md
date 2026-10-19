# moo-l3-vector-api-out: note (attempt 1)

## Blocked: moo-l3-libs-out has not landed

This card removes the float vector from the C API: `lua_pushvector`, `lua_tovector`,
`lua_isvector`, `luaL_checkvector` (and `luaL_optvector`) leave `VM/include/lua.h` and
`VM/include/lualib.h`, their bodies leave `VM/src/lapi.cpp` and `VM/src/laux.cpp`, `CLI/src/Repl.cpp`
stops printing vectors, and `tests/Conformance.test.cpp` stops calling `lua_pushvector`.

The card's premise (SOURCE, DESIGN.md "vec3, quat and transform"): "After the library
(moo-l3-libs-out) and the constant (moo-l3-vector-constant-refused) are gone, only the C API can
still make a vector." The constant is gone (`land moo-l3-vector-constant-refused`).
The library is not.

At the base (`a8f2dfd7`), the float vector library is fully present:
- `VM/src/lveclib.cpp` defines `luaopen_vector` and calls `lua_pushvector`, `luaL_checkvector`,
  `luaL_optvector` throughout, and is built into `Luau.VM` (`Sources.cmake` line 436).
- `VM/src/linit.cpp` registers `LUA_VECLIBNAME`/`luaopen_vector` in both `lualibs` and
  `lualibs_NOINTEGER`, and `luaL_sandbox` pushes a dummy vector for the metatable freeze.
  `VM/src/linit.cpp` is not in this card's PATHS.
- `VM/src/lvector.cpp`, `VM/src/lvector.h` implement the GC vector object.

Removing the four names from the public headers breaks the build of `VM/src/lveclib.cpp` and
`VM/src/linit.cpp`, which call them and are outside this card's PATHS (PATHS: `VM/include/lua.h`,
`VM/include/lualib.h`, `VM/src/lapi.cpp`, `VM/src/laux.cpp`, `CLI/src/Repl.cpp`,
`tests/Conformance.test.cpp`). `tests/Conformance.test.cpp` likewise uses `lua_pushvector` and
`luaL_checkvector` in `setupVectorHelpers` and the vector conformance cases, which the card orders
adapted.

So `DONE-WHEN` cannot hold: the three Luau suites and `Moo.Test` build `Luau.VM`, which links
`lveclib.cpp`. The card should run after `moo-l3-libs-out` lands (it removes `lveclib.cpp`,
`lvector.cpp`/`lvector.h` from `Sources.cmake`, the vector entries from `linit.cpp`, and
`luaopen_vector` from `lualib.h`).

## What was verified (red, unchanged base `a8f2dfd7`)

- `tools/revendor-fixed.sh --check` passes (extern/fixed matches pin `2f1ed913374a`).
- `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLUAU_WERROR=ON
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++` configures.
- `cmake --build build --target Moo.Test Luau.VM` builds; `build/Moo.Test` runs; the only
  failures are the pre-existing named base reds `MooPowIntCertainEverywhere` and
  `MooPowIntVmFractionalBridge` (moo-l4-pow-integer-math/vm), unrelated to vectors.
- `tools/moo-check-no-clock.sh build/libLuau.VM.a` passes.
- `git grep -c 'lua_pushvector' -- VM/include` prints `VM/include/lua.h:2` (the red line).

## Not done

The task itself (the removal and the adaptation) was not done: the card is blocked on the unmet
dependency `moo-l3-libs-out`. No code change was made beyond this note.

## Sentences the closing card must fold in

`docs/DESIGN.md` "Layer 3 as built", TEMPORARY bridges bullet: "the float `vector` type convert
through doubles (`moo_numbridgetodouble`, `moo_numbridgefromdouble`), until the math library is
rebuilt on fixed and the vector type is removed." And "vec3, quat and transform": "Luau's `vector`
is three float32 components packed into the `TValue`. moo removes it in layer 3 (the library, the
type and the bytecode constant, each refused by name)."

`docs/DETERMINISM.md` section 17, "The `vector` type is float32" (`#17`): "in layer 3 the float
vector type is removed. Layer 3b brings `vec3`, `quat` and ..."
