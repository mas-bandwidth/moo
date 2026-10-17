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
the effect log must match bit for bit.

## Status

The inventory below lists every source of non-determinism found in the 0.741 VM, compiler,
codegen and standard library, with its file and line in the fork commit and the fix moo
proposes. Line numbers are as of the fork commit; they move as the files change.

Layer 2 (determinism without numbers, [DESIGN.md](DESIGN.md)) is built. Each fix landed with a
test in `tests/Moo.test.cpp`. This table is the state of `main`:

| # | Source | State |
|---|---|---|
| 1 | object keys hash by address | **fixed**: every object header starts with its 8-byte creation number (`gcseq`); object keys hash by it |
| 4 | GC pacing by clock | **fixed**: no clock in the VM; the heap trigger's growth estimate is the bytes allocated during the last mark phase, and the PI controller is integer arithmetic. `tools/moo-check-no-clock.sh` refuses, at the link level, any VM object but `lperf.cpp` that references a clock, `getenv` or the C random generator; `LUAI_GCMETRICS` does not build |
| 5 | GC accounting by `sizeof` | **fixed, by pinning**: see below |
| 6 | `tostring` shows an address | **fixed**: `table: #123`; light userdata prints `userdata: light`; `moo_objectseq` gives the number to the host |
| 13 | weak tables and destructors | **fixed**: the sweep queues a dead userdata that has a destructor; the queue runs at the end of the same GC step in creation-number order (and at `lua_close`, after everything else is freed). Modelled in `tla/DestructorQueue.tla`; weak entries clear deterministically once 1 and 4 are fixed |
| 9 | `os` and time | **fixed**: the `os` library is deleted (`VM/src/loslib.cpp`) |
| 10 | `math.random` seeding | **fixed**: constant default seed, `moo_setrandomseed` in `VM/include/moo.h` |
| 14 | FMA in native codegen | **fixed**: native code generation is excluded from the build (`MOO_CODEGEN` off, DESIGN.md "Native codegen"); `moo --codegen*` and `--jit-inliner` are refused |

The creation number costs 8 bytes in every object (16 in a userdata, whose data stays 16-byte
aligned at offset 32): a table header is 56 bytes (was 48), a string header 32 (24), a closure
72 (64), an upvalue 48 (40), a buffer header 16 (8). The static asserts in `VM/src/lmem.cpp` pin
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
slots, strings, buffers, userdata, closures, threads; and the GC counters). Both must equal
`tests/determinism/expected.txt` everywhere. The programs: object-keyed tables of every
collectable kind with deletes and rehash (`objectkeys`), allocation churn with weak keys, weak
values, weak both and userdata destructors in five size classes (`gcchurn`), coroutines
(generators, yields across `pcall`, errors, close, wrap, nesting: `coroutines`), and the rest of
the surface without transcendentals (`library`). Measured 2026-10-17, all identical:

| program | effects | state | hulk gcc 13.3 | hulk clang 18.1 | hulk clang Debug ASan+UBSan | mini-m5-1 arm64 Apple clang 21 |
|---|---|---|---|---|---|---|
| coroutines | `c2101c58c9141949` | `d59008475cc0efa8` | same | same | same | same |
| gcchurn | `3fb416325297900d` | `e044604535a6a1e5` | same | same | same | same |
| library | `009401661dac79ec` | `3517c3f5eeac8dfa` | same | same | same | same |
| objectkeys | `c33c0a917acf2ebf` | `9fac96c3362df53c` | same | same | same | same |

`MOO_DETERMINISM_PRINT=1` prints the table's lines. The nightly runs it on Linux gcc, Linux clang
and macOS arm64; CI runs it on Linux clang on every push. Floats are still the number type, so
the suite avoids libm functions and `^`; layer 2 removes that limit.

Upstream tests moo changed, and why:

- `tests/conformance/datetime.luau` tested `os`. It is replaced by the moo-specific expected
  file `tests/conformance/moo/datetime.luau`, which checks that `os` is gone.
- `tests/conformance/sort.luau` timed each sort with `os.clock` and printed the time. The
  timing is removed; the sorts and their checks are unchanged.
- `ApiAllocationRateAfterFullGC` (tests/Conformance.test.cpp) checked that the allocation rate is
  measured in bytes per second. It is replaced by `AllocationRateIsNeverMeasured` in
  tests/Moo.test.cpp, because moo has no clock.
- `tests/conformance/types.luau` checks that the VM's globals match the analyzer's builtin
  definitions. It ignores `_G.os` until the analyzer's definitions drop `os` (owed, with
  `vector` and `integer`, see DESIGN.md "Analyzer").

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
number (with the existing MurmurHash3 finalizer), never by address. Then node placement,
and with it `next`/`pairs` order, depends only on the program.

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
`table: #1234`. Light userdata prints `userdata: light` with no value. The pointer encoding
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

**Fix:** with fixed numbers every conversion is integer arithmetic with a defined result
(saturate, or a named error). Until then this is a real cross-platform divergence for
`bit32` and `string.format` on large values.

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
determinism suite includes heavy-GC programs with destructors and weak tables.

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

**Fix:** in layer 3 the float vector type is removed. Layer 3b brings `vec3`, `quat` and
`transform` back with full Q48.16 components (DESIGN.md, "vec3, quat and transform").

### 18. Float bytes in buffers and string.pack

- `VM/src/lbuflib.cpp:381-390` `buffer.readf32`, `readf64`, `writef32`, `writef64`.
- `VM/src/lstrlib.cpp:1268` and `:1361-1369` `string.pack`/`unpack` formats `f`, `d`, `n`.

These convert between bytes and doubles. **Fix:** keep them, because files and wire
formats carry floats, but convert in integer arithmetic: decode the IEEE bit pattern to
Q48.16 with the one boundary rounding rule (DESIGN.md, "Boundary") and encode Q48.16 to the
nearest float bit pattern the same way. No FPU is involved.

### 19. Bytecode carries doubles

- `Common/include/Luau/Bytecode.h:532` `LBC_CONSTANT_NUMBER` is an 8-byte double, loaded at
  `VM/src/lvmload.cpp:520`.

**Fix:** a moo bytecode version whose number constant is the raw int64 Q48.16 value. The VM
refuses Luau bytecode by version, by name.

### 20. Error messages and tracebacks

- `VM/src/lobject.cpp:130` `luaO_pushvfstring` formats messages with `vsnprintf`. The VM's
  messages use `%s`, `%d` and `%c` only (no `%f`, `%g` or `%p` outside `lgcdebug.cpp`), and
  the strings are type names, chunk names, line numbers and formatted values.
- `VM/src/ldebug.cpp:362`, `:676`, `:692` the same for tracebacks.

Deterministic once number formatting (8) and `tostring` (6) are. **Fix:** route numbers in
messages through moo's formatter; the determinism suite compares error strings and
tracebacks byte for byte.

### 21. Coroutines

Coroutines are cooperative: a coroutine runs only when resumed, and yields only at
`coroutine.yield` or a host yield. There is no preemption, no thread pool and no timer
inside the VM. `coroutine.wrap`, `resume`, `status`, `isyieldable`, `close` and `running`
are functions of the coroutine's state alone. The only external influences are the host's
interrupt callback (`lua_callbacks(L)->interrupt`) and host functions that yield, which are
inputs.

So coroutines are deterministic once everything they compute is. **Proof owed:** a TLA+
model of resume/yield/close/error states (layer 5), and adversarial coroutine programs in
the determinism suite.

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
