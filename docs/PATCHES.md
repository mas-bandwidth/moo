# moo patch inventory

This is the merge checklist from docs/DESIGN.md, "Upstream merges". It lists every upstream
file (Luau 0.741, commit `2d88f9b5facaa9e4ec91247b39ae8251bd49a244`) that moo modifies or deletes,
and why. `tools/moo-check-patches.sh` compares the working tree with that commit and fails when
an upstream file is changed without a row here, or when a row no longer differs. The push gate
runs it on every commit; CI checks out with `fetch-depth: 0` so the fork commit is in history.

Each row names the rule: a section of docs/DESIGN.md or a numbered item of docs/DETERMINISM.md.
moo-added files are listed by directory at the end; they merge cleanly because upstream never
edits them, except where a row above records the seam.

## Upstream files moo modifies or deletes

### VM

| file | why |
|---|---|
| `VM/include/lua.h` | `lua_Number` is `fixed_t` and a host float crosses only through `moo_fromdouble`; the vector C API is removed, and `LUA_TVEC3` follows `LUA_TOBJECT` (DESIGN.md "Boundary", "Layer 3 as built", "vec3, quat and transform"); layer 3b adds the `LUA_TQUAT` tag after `LUA_TVEC3` (DESIGN.md "Layer 3b as built"), and `LUA_TTRANSFORM` after it (DESIGN.md "Layer 3b as built"). |
| `VM/include/lualib.h` | the vector library entry points are removed with the vector API; the name stays defined for merges (DESIGN.md "vec3, quat and transform"); layer 3b declares `luaopen_vec3` and the raw-component vec3 C API `moo_pushvec3`/`moo_tovec3` (DESIGN.md "Layer 3b as built"); `luaopen_quat` and `moo_pushquat`/`moo_toquat` (DESIGN.md "Layer 3b as built"); `luaopen_transform` and `moo_pushtransform`/`moo_totransform` (DESIGN.md "Layer 3b as built"). |
| `VM/src/lapi.cpp` | numbers cross the C API as raw Q48.16, integer arguments refuse a fraction by name, and `lua_pushvector`/`lua_tovector` are gone (DESIGN.md "Boundary", "Exact integers", "vec3, quat and transform"); layer 3b defines `moo_pushvec3` and `moo_tovec3` (DESIGN.md "Layer 3b as built"); `moo_pushquat`/`moo_toquat`, and `lua_topointer` exposes no address for a vec3 or quat (DESIGN.md "Layer 3b as built"); `moo_pushtransform`/`moo_totransform`, and no address for a transform (DESIGN.md "Layer 3b as built"). |
| `VM/src/laux.cpp` | `luaL_checkinteger` and the unsigned variant use the exact-integer rule, and the vector check helpers are removed (DESIGN.md "Exact integers", "vec3, quat and transform"); `tostring` of a vec3 prints `vec3(x, y, z)` (DESIGN.md "Layer 3b as built"); and a quat `quat(x, y, z, s)` (DESIGN.md "Layer 3b as built"); and a transform `transform(vec3(...), quat(...))` (DESIGN.md "Layer 3b as built"). |
| `VM/src/lbaselib.cpp` | `tonumber` reads and returns the raw Q48.16 value and saturates at the int range (DESIGN.md "Literals", "Exact integers"). |
| `VM/src/lbuflib.cpp` | `readf32`/`writef32` and `readf64`/`writef64` use the boundary rounding rule, and buffer offsets require an integer-valued number (DESIGN.md "Boundary", "Exact integers"). |
| `VM/src/lbuiltins.cpp` | fastcalls compute on fixed, use the library's shared trig/exp/log definitions, and the SSE4.1 floor/ceil/round choice is gone; the vector fastcalls are replaced with `luauF_missing` (DESIGN.md "Layer 3 as built", "math library", "vec3, quat and transform"). |
| `VM/src/lclass.cpp` | class member offsets are read and written as raw Q48.16 values through the moo integer conversions (DESIGN.md "Layer 3 as built", "Exact integers"). |
| `VM/src/lcommon.h` | integer-to-number conversion is one saturating Q48.16 helper (DESIGN.md "Layer 3 as built", "Exact integers"). |
| `VM/src/ldo.cpp` | the finalizer count is pushed as a number through the moo cast, not a double (DESIGN.md "Layer 3 as built", "Exact integers"). |
| `VM/src/lgc.cpp` | GC pacing reads no clock and dead userdata destructors run from a queue in creation order at the end of the step (DETERMINISM.md, 4 and 13); a quat is a leaf like a vec3 (DESIGN.md "Layer 3b as built"), and so is a transform (DESIGN.md "Layer 3b as built"). |
| `VM/src/lgc.h` | the object header carries a creation number, so object keys hash by it and `tostring` can print it (DETERMINISM.md, 1 and 6). |
| `VM/src/lgcdebug.cpp` | the heap dump prints numbers with moo's exact formatter instead of the libc double printer (DETERMINISM.md, 8); vec3 and quat are leaves in the validate, dump and enumerate walks (DESIGN.md "Layer 3b as built"), and transform (DESIGN.md "Layer 3b as built"). |
| `VM/src/linit.cpp` | `luaL_openlibs` always uses the table without the integer library, opens no vector library, and the vector sandbox freeze is removed (DESIGN.md "integer", "vec3, quat and transform"); `luaL_openlibs` opens the fixed `vec3` library (DESIGN.md "Layer 3b as built"); and the fixed `quat` library (DESIGN.md "Layer 3b as built"); and the fixed `transform` library (DESIGN.md "Layer 3b as built"). |
| `VM/src/lintlib.cpp` | the integer library converts through the exact-integer and saturating Q48.16 rules (DESIGN.md "Exact integers", "Layer 3 as built"). |
| `VM/src/lmathlib.cpp` | `math` is rebuilt on `fixed` with no libm: exact Q48.16 constants and the fixed compositions for trig, exp/log, pow and the rest (DESIGN.md "math library"). |
| `VM/src/lmem.cpp` | the creation number grows every object header and the size asserts pin the new layout (DETERMINISM.md, 1 and 5); the quat object size is pinned (DESIGN.md "Layer 3b as built"), and the transform's (DESIGN.md "Layer 3b as built"). |
| `VM/src/lnumutils.h` | `luai_num*` are the fixed-point seams: operators, integer checks, overflow errors, `pow` and the exact-integer rule (DESIGN.md "Layer 3 as built", "Overflow", "Exact integers", "math library"). |
| `VM/src/lobject.cpp` | `tonumber` and string coercion use the exact parser, and `tostring` uses the exact formatter (DESIGN.md "Literals", "Number formatting"); quat allocation, value equality and tostring (DESIGN.md "Layer 3b as built"); and transform's (DESIGN.md "Layer 3b as built"). |
| `VM/src/lobject.h` | `TValue` stores the raw Q48.16 number, and the header carries `gcseq`, the destructor-queue page and its link (DETERMINISM.md, 1, 5 and 13; DESIGN.md "Representation"); the quat accessors, and `TKey::tt` widened to 5 bits (DESIGN.md "Layer 3b as built"); the transform accessors (DESIGN.md "Layer 3b as built"). |
| `VM/src/loslib.cpp` | deleted: the `os` library and every clock and time read are removed (DETERMINISM.md, 9). |
| `VM/src/lstate.cpp` | the per-VM creation counter, the destructor queue and the constant default random seed are state, not clocks (DETERMINISM.md, 1, 10 and 13). |
| `VM/src/lstate.h` | the global state carries `gcseq`, the allocation-count pacing fields and the destructor queue (DETERMINISM.md, 1, 4 and 13); `MooQuat` in `union GCObject` (DESIGN.md "Layer 3b as built"), and `MooTransform` (DESIGN.md "Layer 3b as built"). |
| `VM/src/lstrlib.cpp` | integer-only formats require an integer-valued number, and `string.pack`/`unpack` `f`/`d` use the boundary rule (DESIGN.md "Exact integers", "Boundary"). |
| `VM/src/ltable.cpp` | a collectable key hashes by its creation number and a number by its raw value, never by address or IEEE bits (DETERMINISM.md, 1 and 2); and a quat by its four components (DESIGN.md "Layer 3b as built"), and a transform by its seven (DESIGN.md "Layer 3b as built"). |
| `VM/src/ltm.cpp` | `luaT_typenames` names the new `LUA_TVEC3` tag `"vec3"`; interning it at startup shifts every creation number by one (DESIGN.md "vec3, quat and transform", DESIGN.md "Layer 3b as built"); and `quat` (DESIGN.md "Layer 3b as built"), and `transform` (DESIGN.md "Layer 3b as built"). |
| `VM/src/ltablib.cpp` | the table library keeps counts and keys as raw Q48.16 values, not doubles (DESIGN.md "Exact integers"). |
| `VM/src/ludata.cpp` | a dead userdata with a destructor is queued for the end of the GC step instead of running during the sweep (DETERMINISM.md, 13). |
| `VM/src/ludata.h` | userdata holds the destructor-queue page pointer used by the ordered destructor queue (DETERMINISM.md, 13). |
| `VM/src/lveclib.cpp` | the float vector library is out of the build and stays in the tree unbuilt, so upstream merges stay mechanical (DESIGN.md "Layer 3 as built", "The float `vector` is out"). |
| `VM/src/lvmexecute.cpp` | the arithmetic opcodes go through the fixed seams, a checked build raises the named overflow error, and `^` uses the fixed power path and leaves the fast path on `math.pow`'s errors (DESIGN.md "Layer 3 as built", "Overflow", "math library", "pow and ^"); vec3 `==` compares by value in the equality jumps (DESIGN.md "Layer 3b as built"); and quat `==` (DESIGN.md "Layer 3b as built"), and transform `==` (DESIGN.md "Layer 3b as built"). |
| `VM/src/lvmload.cpp` | the loader accepts only the moo bytecode version, reads number constants as raw Q48.16, and refuses a vector constant and `INT64_MIN` by name (DESIGN.md "Literals", "Layer 3 as built", "vec3, quat and transform"). |
| `VM/src/lvmutils.cpp` | division by zero is the named error, a checked overflow stops with the named error, and the numeric `for` trip count is exact with a zero step refused (DESIGN.md "Division by zero", "Overflow", "Exact integers"); vec3 arithmetic, equality and component reads (DESIGN.md "Layer 3b as built"); quat `*` (fixMulQuat, fixRotateVector), equality and component reads (DESIGN.md "Layer 3b as built"); transform `*` (compose, point), equality and `t.p`/`t.q` (DESIGN.md "Layer 3b as built"). |

### Compiler

| file | why |
|---|---|
| `Compiler/src/BuiltinFolding.cpp` | no builtin folds until the math library is rebuilt on fixed; `math.pi` and `math.huge` come from the VM at run time, and vector folding is removed (DESIGN.md "math library", "vec3, quat and transform"). |
| `Compiler/src/Builtins.cpp` | `math.noise` compiles to the fixed shared-path fastcall and all `vector` builtins map to none (DESIGN.md "math library", "vec3, quat and transform"). |
| `Compiler/src/Compiler.cpp` | number constants come from the AST's raw Q48.16 value and no expression emits a vector constant (DESIGN.md "Literals", "Layer 3 as built", "vec3, quat and transform"). |
| `Compiler/src/ConstantFolding.cpp` | folding uses `moo_number.h`, refuses to fold a saturating result in a checked build, and folds integer `^` through `moo_pow_int` (DESIGN.md "Overflow", "math library", "Literals"); an arithmetic or ordering operator on a non-number constant is left to the VM (DESIGN.md "Layer 3 as built"). |
| `Compiler/src/ConstantFolding.h` | a folded number constant is `fixed_t` and there is no vector constant type (DESIGN.md "Layer 3 as built", "The compiler", "vec3, quat and transform"). |
| `Compiler/src/CostModel.cpp` | the cost model converts a raw constant to a double only for its estimate, by the boundary rule (DESIGN.md "Boundary"). |
| `Compiler/src/Types.cpp` | `math.noise`, now a fixed fastcall (`LBF_MATH_NOISE`), resolves to the number type like the other math builtins (DESIGN.md "math library"). |

### Ast

| file | why |
|---|---|
| `Ast/include/Luau/Ast.h` | a number literal carries its raw Q48.16 value and the parser's out-of-range and below-resolution results (DESIGN.md "Literals"). |
| `Ast/src/Ast.cpp` | a node built from a double keeps that double and takes the nearest raw value through the saturating bridge (DESIGN.md "Literals", "Boundary"). |
| `Ast/src/Parser.cpp` | the parser reads a decimal literal as an exact rational rounded once to Q48.16, refuses an integer literal, and reports out of range and below resolution (DESIGN.md "Literals", "integer"). |

### Analysis

| file | why |
|---|---|
| `Analysis/src/BuiltinDefinitions.cpp` | the analyzer declares no `os`, `vector` or `integer` global or metatable (DESIGN.md "Analyzer"). |
| `Analysis/src/EmbeddedBuiltinDefinitions.cpp` | the embedded definitions drop `os`, `vector` and the integer type, including the buffer and integer variants (DESIGN.md "Analyzer", "integer"); layer 3b declares `vec3`, `quat` and `transform` and their libraries (DESIGN.md "Layer 3b as built"). |
| `Analysis/src/GlobalTypes.cpp` | the global type table has no `integer` type (DESIGN.md "Analyzer", "integer"). |
| `Analysis/src/Linter.cpp` | a source literal that is nonzero but rounds to zero is the below-resolution warning; out of range is left to the parser (DESIGN.md "Literals"). |
| `Analysis/src/TypeFunctionRuntime.cpp` | the runtime pushes a zero integer instead of a zero double across the number boundary (DESIGN.md "Boundary"). |
| `Analysis/src/TypeUtils.cpp` | the two `std::any_of` loops are written out so Clang 21 and libstdc++ 15 do not trip `-Wpass-failed` in the Werror build (DESIGN.md "Upstream merges"). |

### Bytecode

| file | why |
|---|---|
| `Common/include/Luau/Bytecode.h` | the moo bytecode version numbers and the raw-number constant encoding are declared here (DESIGN.md "Literals"). |
| `Bytecode/include/Luau/BytecodeBuilder.h` | the builder takes a number constant as its raw Q48.16 value and converts a double once by the boundary rule (DESIGN.md "Literals", "Boundary"). |
| `Bytecode/include/Luau/BytecodeGraph.h` | a graph number constant is raw Q48.16, with the same one-place double conversion (DESIGN.md "Literals", "Boundary"). |
| `Bytecode/include/Luau/Sccp.h` | SCCP's constant is the raw Q48.16 value and its folds use `moo_number.h` exactly as the compiler does, refusing a saturating result in a checked build (DESIGN.md "Literals", "Semantics", "Overflow"). |
| `Bytecode/src/BytecodeBuilder.cpp` | a constant is written as the raw 8-byte little-endian integer, prints through the exact formatter, and no vector constant is written (DESIGN.md "Literals", "Number formatting", "vec3, quat and transform"). |
| `Bytecode/src/BytecodeDump.cpp` | the dump prints a number constant with moo's shortest-round-trip formatter (DESIGN.md "Number formatting"). |
| `Bytecode/src/BytecodeGraph.cpp` | serialization writes and reads the raw 8-byte value and refuses `INT64_MIN` (DESIGN.md "Literals", "Representation"). |
| `Bytecode/src/Sccp.cpp` | SCCP folds with the fixed operations and scales a `LOADN` integer immediate to its raw Q48.16 value (DESIGN.md "Literals", "Semantics", "Exact integers"). |

### CLI

| file | why |
|---|---|
| `CLI/src/FileUtils.cpp` | the CLI file lookup also accepts the `.moo` source extension (DESIGN.md "Layers", item 1). |
| `CLI/src/Repl.cpp` | the REPL reads and prints fixed numbers and refuses native codegen by name (DESIGN.md "Layer 3 as built", "Native codegen"); `--version` prints moo's version (`MOO_VERSION`, `VM/include/moo.h`). |
| `CLI/src/VfsNavigator.cpp` | module lookup tries `.moo` and `init.moo` before `.luau` and `.lua` (DESIGN.md "Layers", item 1). |

### Config

| file | why |
|---|---|
| `Config/src/LuauConfig.cpp` | configuration number keys and values cross the host boundary through `moo_todouble` (DESIGN.md "Boundary"). |

### CMake/Makefile

| file | why |
|---|---|
| `CMakeLists.txt` | native codegen and the JIT inliner are off, FMA contraction is off, the vendored fixed library is built saturating, Moo.Test globs the per-subject test files in tests/moo, and the Windows test binaries get a 2 MB stack in every optimized configuration (DESIGN.md "Native codegen", "Upstream merges", "Overflow", "Tests"); gcc builds `Ast/src/Confusables.cpp` with `-Wno-bidi-chars`, since its bidi characters are confusable data, and `Analysis/src/BuiltinTypeFunctions.cpp` with `-Wno-free-nonheap-object`, a gcc 13 false positive, so `-DLUAU_WERROR=ON` builds with gcc (DESIGN.md "Tests"). |
| `Makefile` | the same codegen/inliner exclusion, `-ffp-contract=off`, the fixed library include and the vector source leaving the build (DESIGN.md "Native codegen", "vec3, quat and transform"). |
| `Sources.cmake` | the vector library, codegen and the JIT inliner leave the source lists, and the fixed sources are added (DESIGN.md "Native codegen", "vec3, quat and transform"); the vec3 library source is added (DESIGN.md "Layer 3b as built"); and the quat library source (DESIGN.md "Layer 3b as built"), and the transform library source (DESIGN.md "Layer 3b as built"); `tests/MooAnalyzer.test.cpp` joins the unit tests (DESIGN.md "Layer 3b as built"). |

### fuzz

| file | why |
|---|---|
| `fuzz/CMakeLists.txt` | the layer 5 differential fuzz targets are built against moo's fixed arithmetic (DESIGN.md "Layers", layer 5; DETERMINISM.md, 7). |
| `fuzz/format.cpp` | the format fuzzer drives moo's exact formatter and parser on raw Q48.16 values, not the libc double paths (DETERMINISM.md, 8). |
| `fuzz/number.cpp` | the number fuzzer drives the exact formatter and parser on raw Q48.16 values (DETERMINISM.md, 7 and 8). |

### tests

| file | why |
|---|---|
| `tests/BytecodeCallInliner.test.cpp` | the test skips the 3-byte moo header, and `10 / 0` stays an unfolded `DIV` where upstream folded it to infinity (DESIGN.md "Literals", "Division by zero"). |
| `tests/BytecodeCompiler.test.cpp` | the test skips the 3-byte moo header and reads raw number constants (DESIGN.md "Literals"). |
| `tests/Compiler.test.cpp` | constant expectations are Q48.16 values and the vector-folding case is skipped (DESIGN.md "Literals", "vec3, quat and transform"). |
| `tests/Conformance.test.cpp` | the vector C API and vector-freeze tests are removed and host floats enter through `MooHost.h` (DESIGN.md "Boundary", "vec3, quat and transform"). |
| `tests/DirectFieldAccess.test.cpp` | the host pushes numbers through `MooHost.h`'s boundary helper (DESIGN.md "Boundary"). |
| `tests/Frontend.test.cpp` | the case that lints literals beyond 2^64 is skipped because those are a parse error in moo (DESIGN.md "Literals"); an upstream `#if 0` case says, with `moo:`, that upstream disables it (DESIGN.md "Tests"). |
| `tests/Linter.test.cpp` | the precision-lint cases on literals beyond 2^53 are skipped because those literals are out of range in moo (DESIGN.md "Literals"). |
| `tests/NonStrictTypeChecker.test.cpp` | an upstream `#if 0` case says, with `moo:`, that upstream disables it, so tools/moo-check-skips.sh accounts for it (DESIGN.md "Tests"). |
| `tests/Normalize.test.cpp` | three upstream `#if 0` cases say, with `moo:`, that upstream disables them, so tools/moo-check-skips.sh accounts for them (DESIGN.md "Tests"). |
| `tests/Parser.test.cpp` | the double-literal and hex/binary-beyond-range cases are skipped because moo parses exactly to Q48.16 (DESIGN.md "Literals"). |
| `tests/PrettyPrinter.test.cpp` | the cases that print literals beyond the range are skipped because those are parse errors in moo (DESIGN.md "Literals"). |
| `tests/Sccp.test.cpp` | folded constants are raw Q48.16 values and the NaN cases are skipped because moo has no NaN (DESIGN.md "Literals", "Representation"). |
| `tests/StringUtils.test.cpp` | an upstream `#if 0` benchmark says, with `moo:`, that upstream disables it, so tools/moo-check-skips.sh accounts for it (DESIGN.md "Tests"). |
| `tests/TypeFunction.test.cpp` | an upstream `#if 0` case says, with `moo:`, that upstream disables it, so tools/moo-check-skips.sh accounts for it (DESIGN.md "Tests"). |
| `tests/TypeFunction.user.test.cpp` | the `integer` builtin cases are skipped because moo has no integer type (DESIGN.md "Analyzer", "integer"). |
| `tests/TypeInfer.builtins.test.cpp` | the `os` and `vector` builtin cases are skipped because the analyzer drops both (DESIGN.md "Analyzer"). |
| `tests/TypeInfer.functions.test.cpp` | the `vector` builtin cases are skipped because the analyzer drops it (DESIGN.md "Analyzer"); an upstream `#if 0` case says, with `moo:`, that upstream disables it (DESIGN.md "Tests"). |
| `tests/TypeInfer.operators.test.cpp` | the `vector` cases are skipped and integer literals are refused (DESIGN.md "Analyzer", "integer"). |
| `tests/TypeInfer.primitives.test.cpp` | the `vector` builtin case is skipped because the analyzer drops it (DESIGN.md "Analyzer"). |
| `tests/TypeInfer.refinements.test.cpp` | the `vector` builtin cases are skipped because the analyzer drops it (DESIGN.md "Analyzer"). |
| `tests/TypeInfer.tables.test.cpp` | the `os` builtin cases are skipped because the analyzer drops it (DESIGN.md "Analyzer"). |
| `tests/TypeInfer.test.cpp` | an upstream `#if 0` case says, with `moo:`, that upstream disables it, so tools/moo-check-skips.sh accounts for it (DESIGN.md "Tests"). |
| `tests/TypeInfer.typestates.test.cpp` | two upstream `#if 0` cases say, with `moo:`, that upstream disables them, so tools/moo-check-skips.sh accounts for them (DESIGN.md "Tests"). |
| `tests/conformance/datetime.luau` | deleted: the case tested `os.date` and `os.time`, which are removed; the moo datetime script replaces it (DETERMINISM.md, 9). |
| `tests/conformance/pcall.luau` | the deep-recursion sums are taken modulo 1000: at depth 2000 they pass the range, which saturates in release and raises in a checked build (DESIGN.md "Overflow"). |
| `tests/conformance/sort.luau` | the timing print uses `os.clock`, so the script no longer measures time (DETERMINISM.md, 9). |
| `tests/conformance/types.luau` | the analyzer and VM no longer define `os` and `vector`, so those globals are ignored rather than required (DESIGN.md "Analyzer", "vec3, quat and transform"); `vec3`, `quat` and `transform` are now checked against the analyzer, and only `quat.slerp`, which the VM refuses by name, is ignored (DESIGN.md "Layer 3b as built"). |
| `tests/require/without_config/types/vector.luau` | the fixture returns a plain table because the vector library is out (DESIGN.md "vec3, quat and transform"). |

### workflows

| file | why |
|---|---|
| `.github/workflows/benchmark.yml` | deleted: the upstream benchmarking pipeline builds Luau's binaries, which moo renames and excludes; moo's gates are `ci.yml` and `nightly.yml` (DESIGN.md "Layers", item 1). |
| `.github/workflows/build.yml` | deleted: replaced by moo's `ci.yml`, whose push gate is the one-minute Moo.Test build (DESIGN.md "Layers", item 1). |
| `.github/workflows/new-release.yml` | deleted: moo does not publish Luau releases; the fork's gates are `ci.yml` and `nightly.yml` (DESIGN.md "Layers", item 1). |
| `.github/workflows/release.yml` | replaced: the upstream release pipeline names Luau's binaries and artifacts, which moo renames (DESIGN.md "Layers", item 1); moo's own `release.yml` publishes a GitHub release from a tag cut by the release tool, with its `CHANGELOG.md` section. |

### README

| file | why |
|---|---|
| `README.md` | describes the moo fork, its promise, the rename and the build, in place of upstream's Luau page (DESIGN.md "Layers", item 1). |

## moo-added files (by directory)

These files are absent upstream, so they merge without conflict. Each directory holds only moo's
own code, tests, notes or vendored `fixed` sources.

- `.github/workflows/` (`ci.yml`, `nightly.yml`)
- `CodeGen/src/` (`moo_codegen_off.cpp`)
- `Common/include/` (`moo_number.h`, `moo_parse.h`, `moo_format.h`, `moo_pow.h`)
- `Inliner/src/` (`moo_jitinliner_off.cpp`)
- `VM/include/` (`moo.h`)
- `VM/src/` (`moo_api.cpp`, `moo_layout.cpp`, and layer 3b's `moo_vec3.h`, `moo_vec3lib.cpp`, `moo_quat.h`, `moo_quatlib.cpp`, `moo_transform.h`, `moo_transformlib.cpp`)
- `docs/` (`DESIGN.md`, `DETERMINISM.md`, `PATCHES.md`, and `history/`, the per-card design notes of layers 3 to 5)
- `extern/fixed/` (the vendored `fixed` library at its pinned commit)
- `fuzz/` (`moo_arith.cpp`)
- `tests/` (`Moo.test.cpp`, `MooCompiler.test.cpp`, `MooAnalyzer.test.cpp`, `MooNumber.test.cpp`, `Determinism.test.cpp`, `MooHost.h`, `MooWide.h`, `moo_main.cpp`)
- `tests/conformance/moo/` (the expected-behaviour scripts, including `spec/`, one per subject)
- `tests/determinism/` (the determinism programs and `expected.txt`)
- `tests/moo/` (one test file per subject, the oracle C file and `vectors/`)
- `tla/` (`NumericFor.tla`, `DestructorQueue.tla` and their models)
- `tools/` (`moo-check-patches.sh`, `moo-check-no-clock.sh`, `moo-check-skips.sh`, `moo-tlc.sh`, `moo-math-oracle/`, `revendor-fixed.sh`)
- `LICENSE-moo.txt`
