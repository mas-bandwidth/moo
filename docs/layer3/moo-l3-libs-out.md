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

- `tests/Conformance.test.cpp` installs a test-only `vector.create` in `setupVectorHelpers`, because
  `udata_direct.luau` is a userdata test that happens to build vector fields; `vector` is still nil
  for every program that does not ask the host for it.
- `tests/conformance/types.luau` still lists `_G.vector` in its ignore set. The ignore is now
  redundant (both the analyzer and the VM omit the global) but harmless: an ignored path is skipped
  whether or not it exists. Removing the entry is outside this card's PATHS.

## Re-pins this card cannot make (files outside its PATHS)

The vector library's startup objects are gone, so every creation number shifts. The values below are
what a checkout with this change actually produces; they remain a function of the program alone and
are the same on every compiler. They must land in a follow-up that owns these files, because this
card's PATHS do not name them (attempt 6 was refused for touching them):

- `tests/determinism/expected.txt`: `coroutines.luau a2351a1850f47a3e eae7f9734f410e3a`,
  `gcchurn.luau 6d789a68240b8976 5ee2eb0d2642bda5`, `library.luau 9ce6b3fc5ed7d5fe 039fbc53f46d1d5a`,
  `objectkeys.luau c026a789bb37c024 38780e98f3910393`, `coroutines2.luau 2ca405a9600143fa dc579a5432f7898e`,
  `errors.luau ee7b3d6f2b3bfb9e 4d4a67e1614c5a55`.
- `tests/Moo.test.cpp`: `ObjectKeyOrderIgnoresAddresses` order
  `43,28,42,57,9,10,34,24,49,22,47,17,59,8,46,58,36,56,13,52,55,54,37,53,1,41,35,50,15,51,60,16,45,2,44,48,40,39,6,32,4,19,12,5,29,21,3`;
  `TostringPrintsCreationNumber` `table: #375 table: #376`;
  `GcPacingIsAFunctionOfTheProgram` `8555275115460150344`;
  `DestructorsRunInCreationOrder` `10826925009745639169`;
  `WeakTableOrderIsAFunctionOfTheProgram` `4589104574784354874`.
- `tests/require/without_config/types/vector.luau` must stop calling `vector.create`; until it does,
  `Luau.CLI.Test`'s `RequireVector` reports `attempt to index nil with 'create'`. Returning
  `{1, 2, 3}` keeps the case's `true, 1, 2, 3` expectation and still exercises the module path.

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
