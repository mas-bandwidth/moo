# moo-l3-libs-out

Removes the float32 vector library and the integer library from `luaL_openlibs`, and the vector
fastcalls from the fastcall table. The analyzer builtin removal (moo-l3-analyzer-builtins) is already
present in the merged base. This
card removes the runtime library and fastcall half; the compiler (moo-l3-vector-compiler-out) and VM
(moo-l3-vector-vm-out) halves remain separate.

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

- `tests/conformance/types.luau` no longer ignores `_G.vector`: the merged analyzer definitions and
  this card's VM removal both omit the global. `_G.os` remains ignored pending the VM's os removal.
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

- docs/DESIGN.md, "Layer 3 as built", the TEMPORARY bridges bullet: "the libm functions of `math`,
  `^`, `math.noise`, and the float `vector` type convert through doubles ..., until the math
  library is rebuilt on fixed and the vector type is removed" must say the vector *library* is
  already out (this card) and only the float `vector` type and bytecode constant remain, owned by
  moo-l3-vector-compiler-out and moo-l3-vector-vm-out.
- docs/DESIGN.md, "Analyzer": "the builtin definitions drop `os`, `vector` and the integer library
  while those are out" must read as already true for `vector` and `integer` in both the analyzer
  and `luaL_openlibs`; only `os` still waits for the VM removal.
- docs/DESIGN.md, "vec3, quat and transform (layer 3b)": "moo removes it in layer 3 (the library,
  the type and the bytecode constant, each refused by name)" must read as the library now gone,
  with the compiler and VM halves (and the analyzer definitions) remaining.
- docs/DETERMINISM.md, 23: already says "the integer library is not opened (Luau already ships
  `lualibs_NOINTEGER`, `VM/src/linit.cpp:28`)". That is now true unconditionally: no
  `FFlag::LuauIntegerLibrary` branch remains in `linit.cpp`. The "still opens" sentence quoted in
  the card does not exist at base b7bca855; no further change is needed here.
