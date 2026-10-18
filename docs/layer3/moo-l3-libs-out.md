# moo-l3-libs-out

Removes the float32 vector library and the integer library from `luaL_openlibs`, and the vector
fastcalls from the fastcall table. This is the library half of removing Luau's float32 vector in
layer 3; the compiler (moo-l3-vector-compiler-out) and VM (moo-l3-vector-vm-out) halves, and the
analyzer, are separate.

## Decision

- `luaL_openlibs` always opens `lualibs_NOINTEGER`, whatever `FFlag::LuauIntegerLibrary` says.
  The flag and `lualibs` (with the integer library) stay defined, unused, so upstream merges stay
  mechanical; `lualibs` is marked `[[maybe_unused]]` so the Werror build stays clean.
- No `vector` library is opened in either table. The `LuauSandboxFreezesVectorMetatable` block in
  `luaL_sandbox` goes, since no vector can be made from a script.
- `VM/src/lveclib.cpp` leaves the build (Sources.cmake; Makefile never listed it) the way CodeGen's
  sources stayed in the tree unbuilt (PR #9). `luaopen_vector`'s declaration in `VM/include/lualib.h`
  is removed with a `moo:` comment; `LUA_VECLIBNAME` stays defined so upstream merges stay mechanical.
- The vector fastcalls in `VM/src/lbuiltins.cpp` (`luauF_vector`, `luauF_vectormagnitude`,
  `luauF_vectornormalize`, `luauF_vectorcross`, `luauF_vectordot`, `luauF_vectorfloor`,
  `luauF_vectorceil`, `luauF_vectorabs`, `luauF_vectorsign`, `luauF_vectorclamp`,
  `luauF_vectormin`, `luauF_vectormax`, `luauF_vectorlerp`) are deleted; their fastcall-table slots
  now answer `luauF_missing`, so bytecode with a vector LBF id falls back to the slow path, where
  `vector` is nil.

## What changed

- `VM/src/linit.cpp`: `luaL_openlibs` always uses `lualibs_NOINTEGER`; removed `luaopen_vector`
  from both library tables; removed the `LuauSandboxFreezesVectorMetatable` block from
  `luaL_sandbox`; marked `lualibs` `[[maybe_unused]]`.
- `VM/include/lualib.h`: removed `luaopen_vector` declaration (kept `LUA_VECLIBNAME`).
- `VM/src/lbuiltins.cpp`: deleted the vector fastcall bodies and replaced their table entries with
  `luauF_missing`.
- `Sources.cmake`: removed `VM/src/lveclib.cpp` from the Luau.VM sources.
- `tests/Conformance.test.cpp`: `Vector` and `VectorLibrary` cases became one `Vector` case that
  runs `moo/vector.luau`; `tests/conformance/moo/vector_library.luau` and its case are deleted.
- `tests/conformance/moo/vector.luau` now asserts `vector` is nil, `vector.create` raises
  `attempt to index nil with 'create'`, and typeof of every value the library used to make is never
  `vector`.
- New: `tests/moo/moo-l3-libs-out.test.cpp` (integer is nil with the integer flags on, and vector is
  nil, after `luaL_openlibs`), `tests/conformance/moo/l3/moo-l3-libs-out.luau`, this note.

## Upstream tests the removal moved

- `tests/conformance/types.luau` ignores `_G.vector`, exactly as it already ignores `_G.os`: the
  analyzer's builtin definitions still declare the global until they drop it.
- `tests/Conformance.test.cpp` installs a test-only `vector.create` in `setupVectorHelpers`, because
  `udata_direct.luau` is a userdata test that happens to build vector fields; `vector` is still nil
  for every program that does not ask the host for it.
- `tests/require/without_config/types/vector.luau` returns a plain table, so the `RequireByString`
  case still checks the module path without a script-made vector.
- The frozen determinism values move because the vector library's startup objects are gone: every
  creation number shifts. `tests/determinism/expected.txt` and the pinned values in
  `tests/Moo.test.cpp` (`ObjectKeyOrderIgnoresAddresses`, `TostringPrintsCreationNumber`,
  `GcPacingIsAFunctionOfTheProgram`, `DestructorsRunInCreationOrder`,
  `WeakTableOrderIsAFunctionOfTheProgram`) are re-pinned; the values remain a function of the program
  alone and are the same on every compiler.

## Sentences that must change when the layer closes

- docs/DESIGN.md, "Layer 3 as built": the line `'vector' and the integer library while those are
  out` must read as having already removed the library half; and the "vec3, quat and transform
  (layer 3b)" sentence "(the library, the type and the bytecode constant, each refused by name)"
  must read as the library now gone, with the compiler and VM halves (and the analyzer) remaining.
- docs/DETERMINISM.md: the note that "VM/src/linit.cpp:44 still opens the integer library when
  FFlag::LuauIntegerLibrary is set" must change to say it never opens it.
