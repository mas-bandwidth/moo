# Determinism in moo

## The guarantee

**The same moo program, given the same inputs, produces bit-identical results and an
identical sequence of observable effects on every supported platform and compiler, for
every language construct and standard library function.**

This is a property of the language, not a sandbox or a style guide. There are no
"don't do that" rules: if a construct is in moo, it is deterministic. If it cannot be made
deterministic, it is removed.

- **Inputs** are: the source or bytecode, the arguments and values the host passes in
  through the C API (including the `math.random` seed), and the results of host functions
  the program calls. Anything else that can influence execution (addresses, clocks, the
  environment, the locale, the CPU's feature set, the C library) is a defect.
- **Observable effects** are: every value a program can compute or inspect, every call to a
  host function with its arguments, every string produced (including `tostring`, error
  messages and tracebacks), the order of table iteration, which weak references are cleared
  and when, and the order and timing of userdata destructors.
- **Supported platforms** are Linux (gcc and clang), macOS arm64 and Windows (MSVC and
  clang-cl), 64-bit only.

The conformance layer (layer 5 in [DESIGN.md](DESIGN.md)) checks the guarantee: every program
in the determinism suite runs on each platform and compiler, and the full VM state hash and
the effect log must match bit for bit. Its libFuzzer targets compare every number operator with
`fixed`'s C and require `parse(format(x)) == x` for arbitrary input.

## Status

The inventory below lists every source of non-determinism found in the 0.741 VM, compiler,
codegen and standard library, with its file and line in the fork commit and the fix moo
proposes. Line numbers are as of the fork commit; they move as the files change.

Layer 2 (determinism without numbers, [DESIGN.md](DESIGN.md)) is built. Each fix landed with a
test in `tests/Moo.test.cpp`. This table is the state of `main`:

| # | Source | State |
|---|---|---|
| 1 | object keys hash by address | **fixed**: every object header starts with its 8-byte creation number (`gcseq`); object keys hash by it, except a vec3, a quat or a transform, which are values and hash by their raw components (layer 3b) |
| 4 | GC pacing by clock | **fixed**: no clock in the VM; the heap trigger's growth estimate is the bytes allocated during the last mark phase, and the PI controller is integer arithmetic. `tools/moo-check-no-clock.sh` refuses, at the link level, any VM object but `lperf.cpp` that references a clock, `getenv` or the C random generator; `LUAI_GCMETRICS` does not build |
| 5 | GC accounting by `sizeof` | **fixed, by pinning**: see below |
| 6 | `tostring` shows an address | **fixed**: `table: #123`; light userdata prints `userdata: light`; `moo_objectseq` gives the number to the host |
| 13 | weak tables and destructors | **fixed**: the sweep queues a dead userdata that has a destructor; the queue runs at the end of the same GC step in creation-number order (and at `lua_close`, after everything else is freed). Modelled in `tla/DestructorQueue.tla`; weak entries clear deterministically once 1 and 4 are fixed |
| 2 | number keys hash by IEEE bits | **fixed**: numbers hash by their raw Q48.16 value |
| 7 | platform libm | **partly**: `+ - * / // %` are fixed's integer arithmetic, and the compiler folds with the same functions, checked bit-exact against fixed's C on every path by the `moo-oracle` suite; the libm functions and `^` still cross a double bridge until the math library is rebuilt on fixed (layer 4) |
| 11 | out-of-range float-to-int casts | **fixed**: every number-to-integer conversion requires an integer-valued number (`moo_numisinteger` / `luai_numisint`, raising `number has no integer representation` in library calls and falling back in fastcalls) and saturates at the C integer range (`moo_number.h`) |
| 8 | number formatting and parsing through libc | **fixed**: one exact formatter (`moo_format.h`) and one exact parser (`moo_parse.h`), no `printf`, `strtod` or locale |
| 9 | `os` and time | **fixed**: the `os` library is deleted (`VM/src/loslib.cpp`) |
| 10 | `math.random` seeding | **fixed**: constant default seed, `moo_setrandomseed` in `VM/include/moo.h` |
| 14 | FMA in native codegen | **fixed**: native code generation is excluded from the build (`MOO_CODEGEN` off, DESIGN.md "Native codegen"); `moo --codegen*` and `--jit-inliner` are refused |

The creation number costs 8 bytes in every object (16 in a userdata, whose data stays 16-byte
aligned at offset 32): a table header is 56 bytes (was 48), a string header 32 (24), a closure
72 (64), an upvalue 48 (40), a buffer header 16 (8), a vec3 40 (32), a quat 48 (40), a transform 72 (64). The static asserts in `VM/src/lmem.cpp` pin
these sizes, and moo now refuses to build for a 32-bit target.

**Allocation units (5), as built.** The proposal below was a separate unit counter with its own
per-kind formula. What is built keeps one counter, `totalbytes`, because it already is a fixed
formula: lmem.cpp adds the size the VM requested (never the size class or the host allocator's
overhead), and every request is a struct size plus a payload length the program chose. The struct
sizes are what C does not guarantee, so `VM/src/moo_layout.cpp` pins every one of them with a
`static_assert`; a compiler that lays any of them out differently does not build moo. One counter
means no second set of conversions in the GC's work accounting (which is also in these bytes), and
no change to the hot allocation path. `lua_gc` and `gcinfo` report it, so both are deterministic.
`lua_allocationrate` returns -1 (cannot be measured), always.

**The determinism suite.** `tests/Determinism.test.cpp` (doctest `DeterminismSuite`, in
`Moo.Test`) runs each adversarial program in `tests/determinism` in a fresh VM with a fixed
host seed and logging host functions, twice, and takes two hashes: the **effects** (every log line,
every destructor with the GC step boundaries, `lua_close`'s destructors) and the **state** (every GC
object's header mid-cycle, then after a full collection every object's header and contents: table
slots, strings, buffers, userdata, closures, threads, and the components of a vec3, a quat or a transform; and the
GC counters). Both must equal
`tests/determinism/expected.txt` everywhere. The programs: object-keyed tables of every
collectable kind with deletes and rehash (`objectkeys`), allocation churn with weak keys, weak
values, weak both and userdata destructors in five size classes (`gcchurn`), coroutines
(generators, yields across `pcall`, errors, close, wrap, nesting: `coroutines`, and the
adversarial transitions and refusals in `coroutines2`), every error string and traceback
(`errors`), the rest of the surface without transcendentals (`library`), and the math oracle's
programs (`math`). Every platform cell is `same` or `pending nightly`:

| program | effects | state | hulk gcc 15.2 | hulk clang 21.1 | hulk clang Debug ASan+UBSan | mini-m5-1 arm64 Apple clang 21 | windows-msvc | windows-clang-cl |
|---|---|---|---|---|---|---|---|---|
| coroutines | `5cb7f17a10d68cc4` | `6fad6a2c9bf8a173` | pending nightly | pending nightly | pending nightly | pending nightly | pending nightly | pending nightly |
| coroutines2 | `45af4a6f2428da59` | `5359668bdc22dc58` | pending nightly | pending nightly | pending nightly | pending nightly | pending nightly | pending nightly |
| errors | `f7037948e0d4bea2` | `be21c103205a5f40` | pending nightly | pending nightly | pending nightly | pending nightly | pending nightly | pending nightly |
| gcchurn | `971f095d77280529` | `99a32dc86877a486` | pending nightly | pending nightly | pending nightly | pending nightly | pending nightly | pending nightly |
| library | `b37b92ae87f532ba` | `d8e5f88d20c961f8` | pending nightly | pending nightly | pending nightly | pending nightly | pending nightly | pending nightly |
| math | `0d7b989a30435fce` | `6f1e734e163b3843` | pending nightly | pending nightly | pending nightly | pending nightly | pending nightly | pending nightly |
| objectkeys | `1067f47a944c9471` | `673230ee8047eeaa` | pending nightly | pending nightly | pending nightly | pending nightly | pending nightly | pending nightly |

The last full nightly-workflow matrix the coordinator dispatched was
[run 39176151564](https://github.com/mas-bandwidth/moo/actions/runs/39176151564), created
2026-10-18 23:21:11Z at head `291f70d6a80c9ecc9a1999fcd84bf82688978fe1`, on the layer 5 branch
before this tree's re-pin; TLC and the math-oracle job passed. Linux gcc, Linux clang, macOS arm64 and
Windows clang-cl built and ran `Moo.Test` and reported no determinism-hash mismatch. Windows MSVC
failed to build at `tests/moo/moo-l4-math-exact-ops.test.cpp` (C2059), before its tests ran. The
sanitizer configuration is not in the scheduled matrix and remains pending. Because the pinned
hashes moved after that run, the table records `same` only where this card re-ran the suite on the
Linux bench (gcc 15.2 and clang 21.1) and `pending nightly` elsewhere. The earlier scheduled
[run 39107758768](https://github.com/mas-bandwidth/moo/actions/runs/39107758768), created
2026-10-18 07:27:59Z at head `8317486459d8b67e2b4a4f4685e31141d3ee941b`, passed the four programs
then present on Linux gcc, Linux clang, macOS arm64 and Windows MSVC; that Windows job took 8m23s.

Re-pinned again when layer 3b added the vec3 tag: its type-name string shifts every creation number by one. Re-pinned
again when the vec3 library joined `luaL_openlibs`, when layer 3b added the quat tag and library, and when it added the
transform tag and library: each type-name string, library table and function name is a startup object, so every
creation number shifts, and each tag grows `global_State` by two tag slots (one in `mt[]`, one in `ttname[]`, 16
bytes), which moves GC pacing. The mechanism (creation number, not address) is unchanged. The old and new values of
each re-pin are in `docs/layer3b/` (`moo-v2-l3b-vec3-object.md`, `moo-v2-l3b-vec3-ops.md`, `moo-v2-l3b-quat.md`,
`moo-v2-l3b-transform.md`). The vec3 library and quat re-pins were identical on clang 18 and gcc 13; the transform
note does not name its compilers. The table's platform columns have not been re-run at these values, so every cell
is `pending nightly`.

The closing card re-verified the Linux gate at this tree: `tools/revendor-fixed.sh --check` is
clean, the no-clock check is clean, and `Moo.Test` passes all 175 cases on both gcc and clang, so
there is no cross-compiler divergence. `Luau.Conformance` passes (106 cases) and `Luau.CLI.Test`
passes (113 cases). `Luau.UnitTest` has one pre-existing failure, `fold_constants_div_by_zero` in
`tests/BytecodeCallInliner.test.cpp` (5147 of 5148 cases passed, 55 skipped); that upstream file is
unchanged by this docs-only card and fails the same way at BASE.

`MOO_DETERMINISM_PRINT=1` prints the table's lines. The nightly runs it on Linux gcc, Linux clang,
macOS arm64 and Windows MSVC and clang-cl, and runs the `moo-oracle` suite at `MOO_ORACLE_N=2^24`
on all four; CI runs it on Linux clang on every push.

When tostring became the shortest exact round trip, `strconv.luau`'s cases that expect a double's digits were marked.
When the `for` trip count became exact, `interrupt.luau` moved (its endless loops used a zero step, now an error) and
`basic.luau` lost its zero-step cases. When literals became exact, `strings.luau` and `attrib.luau` moved too (literals beyond the range are compile errors),
nine parser, linter, pretty-printer and frontend cases that assert double literals or precision lints are skipped with
the reason beside each, and four compiler cases had their expected decimal constants updated to the Q48.16 values,
marked `// moo:`. `tools/moo-check-skips.sh` lists every doctest skip, marked early return and moved script; it
fails when one lacks a `moo:` reason or names a replacement that does not exist.

Upstream tests moo changed when number became Q48.16, and why: each moved script is a moo-specific expected file in
`tests/conformance/moo/` (basic, buffers, math, tables, errors, events, bitwise, tpack, debug, strconv, iter, vector).
Its upstream lines are kept, and every changed line is marked `-- moo:` with its reason (no infinity, NaN or negative
zero; division by zero is an error; the range is about +-1.4e14; values round to 2^-16; tostring prints the shortest
exact decimal). The note at the end of each file states the reason for the whole file, and the harness keeps each
file's upstream chunk name. Eleven compiler cases that asserted `math.*`, `bit32.*` or `^` folding moo turned off are
skipped in `tests/Compiler.test.cpp`, each pointing at `tests/MooCompiler.test.cpp`, which tests moo's folding, and
eleven compiler cases that asserted float `vector` folding, constants or fastcalls are skipped with
`// moo: the float vector is removed (DESIGN.md "vec3, quat and transform")`. Three Sccp cases that build a NaN
constant (`tests/Sccp.test.cpp` `sccp_nan_compare_check1/2/3`) and `NativeIntegerSpills`
(`tests/Conformance.test.cpp`, whose integer constant the loader refuses by name) are skipped with the reason beside
each. The same audit reports eleven upstream `#if 0` blocks that predate moo and still carry their upstream ticket
comment; they are listed for a follow-up, not counted as moo skips. `MOO_CONFORMANCE_SCAN=1` makes the conformance harness's `assert` report and continue, to list every failing
line of a script at once.

Upstream tests moo changed, and why:

- `tests/conformance/datetime.luau` tested `os`. It is replaced by the moo-specific expected
  file `tests/conformance/moo/datetime.luau`, which checks that `os` is gone.
- `tests/conformance/sort.luau` timed each sort with `os.clock` and printed the time. The
  timing is removed; the sorts and their checks are unchanged.
- `ApiAllocationRateAfterFullGC` (tests/Conformance.test.cpp) checked that the allocation rate is
  measured in bytes per second. It is replaced by `AllocationRateIsNeverMeasured` in
  tests/Moo.test.cpp, because moo has no clock.
- `tests/conformance/types.luau` checks that the VM's globals match the analyzer's builtin
  definitions. The `_G.os` ignore is gone; `_G.vector` is ignored until the VM removal lands (see
  DESIGN.md "Analyzer"). `_G.vec3`, `_G.quat` and `_G.transform` are checked on both sides; only
  `_G.quat.slerp` is ignored, because the VM refuses it by name and the analyzer does not declare it.

## Inventory

### 1. Object keys hash by address, so `pairs`/`next` order varies

- `VM/src/ltable.cpp:72` `hashpointer` hashes the low 32 bits of the object's address.
- `VM/src/ltable.cpp:216-228` (`mainposition`) uses it for light userdata and for every
  collectable key: tables, functions, userdata, threads, buffers.
- `VM/src/ltable.cpp:247`, `:277`, `:1240` are the same hash on the pointer fast paths.
- `VM/src/ltable.cpp:379` `luaH_next` walks the array part, then the hash part in node
  order (`:391`). Node order is the hash order, so with object keys the iteration order
  depends on where the allocator put each object: it changes with the platform, the
  allocator, ASLR and the program's earlier allocations.

**Fix:** every collectable object gets a 64-bit **creation sequence number**, taken from a
per-VM counter at allocation and stored in the object header. Object keys hash by that
number (with the existing MurmurHash3 finalizer), never by address. A vec3 is the exception:
it is a value, equal vec3 are one key, and it hashes by its three raw components
(`moo_vec3_hash`). A quat is the exception too: equal quat are one key, and it hashes by its four
raw components (`moo_quat_hash`). So is a transform, by its seven raw components
(`moo_transform_hash`). Then node placement, and with it `next`/`pairs` order, depends only on the
program.

**Rehash and resize need no change.** `resize` (`VM/src/ltable.cpp:573`) reinserts the old
nodes in a fixed slot order and collisions take the free slot found by a fixed downward
scan, so once every hash input is deterministic the layout after any sequence of inserts,
deletes and resizes is a pure function of that sequence. The iteration order is then
documented as: the array part in index order, then the hash part in node order, where the
node layout is a function of the table's insertion and resize history. The determinism
suite pins it with adversarial object-keyed tables.

**Light userdata** keys hash by their value. Their value is a host input; a host that passes
addresses as light userdata is passing non-deterministic input. moo documents this in the
C API, and `tostring` never prints a light userdata value (see 6).

### 2. Number keys hash by IEEE bits

- `VM/src/ltable.cpp:90` `hashnum` hashes the two 32-bit halves of the double.

This one is deterministic today (IEEE-754 bits are the same everywhere), but it goes away
with floats. **Fix:** hash the raw int64 of the Q48.16 value (layer 3).

### 3. String hashing

- `VM/src/lstring.cpp:10` `luaS_hash` has no random seed; the compiler replicates it
  (`Compiler` `BytecodeBuilder::getStringHash`, checked by `tests/Conformance.test.cpp:2521`).

Already deterministic. **Fix:** none; the guarantee is that strings hash with this fixed
function and a fixed seed of zero, and moo never adds a random seed.

### 4. GC pacing by clock

- `VM/src/lperf.cpp:65` `lua_clock` reads `CLOCK_MONOTONIC` (or `clock()`).
- `VM/src/lgc.cpp:209`, `:217`, `:223`, `:1012`, `:1197`, `:1201`, `:1326`, `:1332`,
  `:1349`, `:1364`, `:1452`, `:1535` read it for GC timestamps.
- `VM/src/lgc.cpp:1288` `getheaptrigger` uses the measured allocation **rate** (bytes per
  second, from those timestamps) to decide when the next cycle starts. So when a cycle
  starts, and therefore when weak references clear and when destructors run, depends on
  how fast the machine is.
- `VM/src/lgc.cpp:1250` `getheaptriggererroroffset` is a PI controller in doubles.
- `VM/src/lgc.cpp:1528` `luaC_allocationrate` is exposed through `lua_gc`
  (`VM/src/lapi.cpp:2182`).

**Fix:** no clock anywhere in the VM. GC pacing is a function of **allocation units** only:
the heap trigger, step size and goal are integer arithmetic on a counter. The PI controller
becomes integer arithmetic on the same counter, or is removed. `lua_clock` and the GC
timing metrics are deleted from the VM (a host that wants profiling times the calls it makes).

### 5. GC accounting by `sizeof`

- `VM/src/lgc.cpp:1318-1377` pace by `totalbytes`, which is the sum of allocation sizes in
  bytes. Object sizes come from `sizeof` and pointer width, so the same program counts a
  different number of bytes on a different ABI.

On today's 64-bit targets the struct layouts happen to match, but nothing guarantees it.
**Fix:** pacing counts **allocation units** computed by a fixed, platform-independent formula
per object kind and payload length (for example: a table is 1 unit plus 1 per array slot
and 1 per node; a string is 1 unit plus its length divided by 16). `totalbytes` stays for
the host's memory reporting but no longer drives anything observable.

### 6. `tostring` of tables, functions, userdata and threads shows an address

- `VM/src/laux.cpp:669-671` prints `"<type>: 0x%016llx"` of `lua_encodepointer(lua_topointer(...))`.
- `VM/src/lapi.cpp:1831` `lua_encodepointer` is the identity until the host calls
  `lua_setpointerencodekey` (`VM/src/lapi.cpp:1804`); with a key it is a keyed hash of the
  address. Either way the result depends on the address.

**Fix:** `tostring` of a reference value prints its creation sequence number, for example
`table: #1234`. Light userdata prints `userdata: light` with no value. A vec3 prints
`vec3(x, y, z)` through the exact formatter, a quat `quat(x, y, z, s)` and a transform
`transform(vec3(px, py, pz), quat(qx, qy, qz, qs))`; `lua_topointer` returns `NULL` for all three.
The pointer encoding
key and `lua_encodepointer` stay in the C API for upstream compatibility but nothing
script-visible uses them.

### 7. Platform libm in math, `^` and compile-time folding

The VM calls the C library's transcendental functions, whose last-bit results differ
between glibc, Apple's libm and the MSVC CRT:

- `VM/src/lmathlib.cpp:47-200`: `sin`, `sinh`, `cos`, `cosh`, `tan`, `tanh`, `asin`,
  `acos`, `atan`, `atan2`, `sqrt` (`:136`), `pow` (`:142`), `log`/`log2`/`log10`
  (`:146-168`), `exp` (`:174`), `fmod`, `modf`, `frexp`, `ldexp`, and `round` (`:421`).
- `VM/src/lmathlib.cpp:369` `math.noise` (Perlin noise in doubles), `:438` `math.lerp`.
- `VM/src/lbuiltins.cpp:60` onward: the fastcall versions of the same functions (`acos`
  at `:65`, `atan2` at `:90`, `cos` at `:140`, `exp` at `:165`, `fmod` at `:192`, ...).
- `VM/src/lnumutils.h:12` `luai_numpow` is `pow`; the `^` operator uses it in
  `VM/src/lvmutils.cpp:698` and directly in `VM/src/lvmexecute.cpp:2061` and `:2296`
  (`:2296` special-cases exponents 2, 0.5 and 3 and calls `pow` otherwise).
- `VM/src/lnumutils.h:81`, `:88`: `%` and `//` use `floor`.
- **The compiler folds constants with the build machine's libm**:
  `Compiler/src/BuiltinFolding.cpp:160-301` and `:595` (every `math.*` call with constant
  arguments), `Compiler/src/ConstantFolding.cpp:366` (`//`) and `:466` (`^`). So the
  bytecode itself depends on the platform that compiled it.

**Fix:** floats leave the VM (layer 3) and the math library is rebuilt on `fixed`
(layer 4): `sin`, `cos`, `atan2`, `sqrt`, `exp2`, `log2` from `fixed`, with the rest built
from them in integer arithmetic. The compiler's folding calls the **same** fixed functions
the VM calls, so folded and unfolded code agree bit for bit. See DESIGN.md, "math library".

### 8. Number formatting and parsing through libc

- `VM/src/lnumprint.cpp:273` `luai_num2str` (Schubfach shortest double printing). It is
  integer code and deterministic, but it formats doubles.
- `VM/src/lstrlib.cpp:1035-1041`: `string.format` `%e`, `%f`, `%g` go to `snprintf` with
  a double. Output for halfway cases and for precision beyond 17 digits differs between C
  libraries.
- `VM/src/lstrlib.cpp:1004` and `:1008-1032`: `%c`, `%d`, `%i`, `%x` cast the double to an
  integer first (see 11).
- `VM/src/lobject.cpp:86` `luaO_str2d` and `VM/src/lnumutils.h:133` `luai_str2num`:
  `tonumber` and string-to-number coercion use `strtod`, which **depends on the C locale**
  (the decimal separator) and, historically, on the C library's rounding.
  `VM/src/lobject.cpp:93` uses `strtoul` for hex.
- `VM/src/lgcdebug.cpp:827` prints `%.14g` in heap dumps (host debug output, not script-visible).

**Fix:** one deterministic integer formatter and one deterministic integer parser for
Q48.16, both in moo, no libc. `tostring` and `string.format` use them. See DESIGN.md,
"Number formatting" and "string.format".

### 9. `os` and time

- `VM/src/loslib.cpp:213-217` registers `os.clock`, `os.date`, `os.difftime`, `os.time`.
- `VM/src/loslib.cpp:61` `os.clock` returns `lua_clock()`; `:115` and `:179` call
  `time(NULL)`; `os.date` formats with the host time zone and locale.

**Fix:** the `os` library is removed. Luau has no `io` library; moo never adds one. Time is
an input: a host that wants it passes it in.

### 10. `math.random` seeded from address, time and clock

- `VM/src/lmathlib.cpp:519-523` `luaopen_math` seeds PCG32 with
  `lua_encodepointer(L) ^ time(NULL) ^ clock()`.
- `VM/src/lmathlib.cpp:234-245` `math.random` builds a double with `ldexp`.

**Fix:** the generator stays PCG32 (it is integer and deterministic). The default seed is a
constant, and the host sets the seed through a new C API call. `math.randomseed` stays.
`math.random` returns fixed values built from the generator's integer output.

### 11. Out-of-range float-to-integer casts

- `VM/src/lnumutils.h:102` `luai_num2int` is `(int)(d)`, which is undefined behaviour out
  of range: x86 gives `INT_MIN`, arm64 saturates.
- `VM/src/lnumutils.h:114-124` `luai_num2unsigned` goes through `long long`, with a
  separate x87 path for 32-bit MSVC; used by `bit32` and `string.format` `%x`.

**Fix:** with fixed numbers every conversion is integer arithmetic with a defined result:
non-integer numbers are refused by `lua_tointegerx` / `lua_tounsignedx` and by `luai_numisint`
in every `VM/src/lbuiltins.cpp` fastcall (raising `number has no integer representation`),
and integer-valued numbers outside the 32-bit C range saturate (`luai_num2int`) or reduce
modulo 2^32 (`luai_num2unsigned`).

### 12. `collectgarbage` and `gcinfo`

- `VM/src/lbaselib.cpp:194` `gcinfo` returns the heap size in KB, which depends on `sizeof`
  and the allocator.
- `CLI/src/Repl.cpp:121` the CLI's `collectgarbage("collect")` and `("count")`. Luau's
  base library does not have `collectgarbage`; only the CLI and the test harness
  (`tests/Conformance.test.cpp:345`) register it.

**Fix:** `gcinfo` returns allocation units (see 5), which are deterministic. moo's
`collectgarbage` accepts only `"collect"` (a full cycle, deterministic once pacing and
finalization are) and `"count"` (allocation units). Any other option is a named error.

### 13. Weak tables and userdata destructors

- `VM/src/lgc.cpp:336-355` reads `__mode`; `:712` `cleartable` clears weak entries whose
  key or value was not marked.
- Luau has no script `__gc`. Userdata destructors are C functions registered by the host
  (`VM/src/lapi.cpp:1671` `lua_newuserdatadtor`, `:1925` `lua_setuserdatadtor`) and run
  from the sweep (`VM/src/ludata.cpp:23-35`, called at `VM/src/lgc.cpp:796`).
- The sweep walks the GC pages in list order (`VM/src/lgc.cpp:1097`, `:1211-1223`). Page
  order and the slot order inside a page follow allocation history, not addresses, but
  that has not been proven, and sweep is incremental, so where a destructor falls between
  instructions depends on pacing (4 and 5).

Weak tables and destructors are kept: weak tables are how caches and back-references are
written in Luau, and destructors are how the host frees what its userdata own. Once pacing
depends only on allocation units, which cycle clears which weak entry is a function of the
program. **Fix:** destructors no longer run inside the sweep. The sweep queues dead
userdata; at the end of the GC step the queue runs in **creation-sequence order**. The
determinism suite includes heavy-GC programs with destructors and weak tables. A vec3, a quat or
a transform, like a string, is a value and is never cleared from a weak table.

### 14. FMA in native codegen

- `CodeGen/src/IrLoweringX64.cpp:791-821`: `MULADD_NUM` (emitted for `math.lerp` at
  `CodeGen/src/IrTranslateBuiltins.cpp:390`) is a fused `vfmadd213pd` **only if the CPU
  reports FMA3** (`CodeGen/src/CodeGen.cpp:186`), and two roundings otherwise. So native
  `math.lerp` gives different bits on two x64 machines, and different bits from the
  interpreter.
- `CodeGen/src/IrLoweringX64.cpp:1357-1362` the same for `MULADD_VEC` (`vfmadd213ps`).
- `CodeGen/src/IrLoweringA64.cpp:866-877` and `:1142-1153`: arm64 always fuses (`fmla`).

**Fix:** native codegen is off in moo (interpreter only) until it is rebuilt on the fixed
type, which has no FMA. For the C++ compiler, the fork already builds everything with
`-ffp-contract=off`; MSVC does not contract under its default `/fp:precise`.

### 15. x87 and 32-bit targets

- `VM/src/lnumutils.h:114-124` is a 32-bit MSVC x87 path. On 32-bit x86 without SSE2,
  doubles can be computed in 80-bit registers and rounded at spill points the compiler
  chooses.

**Fix:** moo supports 64-bit targets only. Once floats are gone from the VM, x87 cannot
affect it.

### 16. Table length (`#`) on tables with holes

- `VM/src/ltable.cpp:1470` `luaH_getn` returns *a* border: it uses a cached boundary hint
  (`getaboundary`), then a binary search of the array part, then a search of the hash part.

This is deterministic: the hint, the array size and the search are functions of the
table's history, with no address or float in them. It is unspecified, like Lua, not random.
**Fix:** none in the VM; the guarantee documents that `#t` on a table with holes is a
deterministic function of the table's history, and the determinism suite pins it.

### 17. The `vector` type is float32

- `VM/src/lobject.h:39` stores vector components as `float`; `VM/include/luaconf.h:137`
  `LUA_VECTOR_SIZE` is 3. `VM/src/lveclib.cpp` and the compiler's vector folding
  (`Compiler/src/ConstantFolding.cpp:371-433`) compute in float.

**Fixed in layer 3:** the float vector type is removed. The library, the bytecode constant and
the C API (`lua_pushvector`, `lua_tovector`, `lua_isvector`, `luaL_checkvector`) are removed
first; `LUA_TVECTOR` stays unused in the tag enum for upstream merges; vec3, quat and transform get
new tags after `LUA_TOBJECT` (`docs/layer3b/moo-v2-l3b-vec3-object.md`): `LUA_TVEC3` 14,
`LUA_TQUAT` 15 and `LUA_TTRANSFORM` 16, and `TKey::tt` is 5 bits so `LUA_TDEADKEY` 17 fits. Layer 3b brings `vec3`,
`quat` and `transform` back with full Q48.16 components (DESIGN.md, "vec3, quat and transform").

### 18. Float bytes in buffers and string.pack

- `VM/src/lbuflib.cpp:381-390` `buffer.readf32`, `readf64`, `writef32`, `writef64`.
- `VM/src/lstrlib.cpp:1268` and `:1361-1369` `string.pack`/`unpack` formats `f`, `d`, `n`.

**Fixed in layer 3:** they convert in integer arithmetic: decode the IEEE bit pattern to Q48.16
with the one boundary rounding rule (`moo_numfromdouble`; a NaN, infinity or out-of-range value
raises a named error) and encode Q48.16 to the nearest float bit pattern the same way
(`moo_numtofloatbits` / `moo_numtodoublebits`); `string.pack` format `n` is the raw Q48.16 int64.
No FPU is involved.

### 19. Bytecode carries doubles

- `Common/include/Luau/Bytecode.h:532` `LBC_CONSTANT_NUMBER` is an 8-byte double, loaded at
  `VM/src/lvmload.cpp:520`.

**Fix:** a moo bytecode version whose number constant is the raw int64 Q48.16 value. The VM
refuses Luau bytecode by version, by name. Built in layer 3 (`moo-l3-bytecode-version`): moo
bytecode version 1; `VM/src/lvmload.cpp` reads the raw value and refuses Luau bytecode by name.

### 20. Error messages and tracebacks

- `VM/src/lobject.cpp:130` `luaO_pushvfstring` formats messages with `vsnprintf`. The VM's
  messages use `%s`, `%d` and `%c` only (no `%f`, `%g` or `%p` outside `lgcdebug.cpp`), and
  the strings are type names, chunk names, line numbers and formatted values.
- `VM/src/ldebug.cpp:362`, `:676`, `:692` the same for tracebacks.

The layer 5 audit found no numeric conversion specifier in the VM's error or traceback formats
(only integer line and frame counts), so no change was needed; `tests/determinism/errors.luau`
pins the error strings and tracebacks byte for byte in the determinism suite.

### 21. Coroutines

Coroutines are cooperative: a coroutine runs only when resumed, and yields only at
`coroutine.yield` or a host yield. There is no preemption, no thread pool and no timer
inside the VM. `coroutine.wrap`, `resume`, `status`, `isyieldable`, `close` and `running`
are functions of the coroutine's state alone. The only external influences are the host's
interrupt callback (`lua_callbacks(L)->interrupt`) and host functions that yield, which are
inputs.

So coroutines are deterministic once everything they compute is. The adversarial programs in the
determinism suite are `coroutines` and `coroutines2` (`tests/determinism/coroutines2.luau`),
and the conformance script and C++ cases in `tests/conformance/moo/l3/moo-l5-coroutine-programs.luau`
and `tests/moo/moo-l5-coroutine-programs.test.cpp` pin the transitions at the C API boundary. The
TLA+ model of resume/yield/close/error states (`tla/Coroutine.tla`) is owed.

### 22. Host-dependent limits

- Out-of-memory points depend on the host allocator and its limit: an input.
- Stack and call depth limits (`LUAI_MAXCCALLS`, `LUAI_MAXCSTACK`) are counters, not
  measurements of the native stack, so they are deterministic.

**Fix:** document memory limits as a host input; the allocation-unit counter (5) lets a
host set a deterministic limit.

### 23. The `integer` type

- `VM/include/lua.h:81` `LUA_TINTEGER` and `VM/src/linit.cpp:24` `luaopen_integer`:
  Luau 0.741 has an experimental int64 type. It is integer arithmetic and deterministic.

**Fix:** see DESIGN.md, "integer". moo has one number type; the integer library is not
opened (Luau already ships `lualibs_NOINTEGER`, `VM/src/linit.cpp:28`).

### 24. The bytecode-graph constant propagation folds doubles (Sccp)

`Bytecode/include/Luau/Sccp.h:70` `evaluateNumberBinaryOp` and `Bytecode/src/Sccp.cpp:26`
(`BcVmConstImpl::evaluate`) folded number constants in double arithmetic, so a folded constant
was the boundary-rounded double, not the VM's Q48.16 result, and could differ from run-time
arithmetic or saturate wrongly.

**Fix:** built in layer 3 (`moo-l3-bytecode-graph-fixed`): Sccp folds on the raw Q48.16 value
with the same `moo_number.h` operations the compiler and VM use; division and `%` by zero and
`^` are never folded.
