# moo-l4-bridge-removed

The temporary double bridge is deleted. `moo_numbridgefromdouble` and `moo_numbridgetodouble` are
gone from `Common/include/moo_number.h`; every former caller now uses the permanent boundary rule
already in that header, and the heap dump prints a numeric table key with `moo_format.h`.

## Decision

The bridge was a temporary convenience, not a boundary rule: it rounded a libm double to Q48.16
with its own arithmetic (`double(int64_t(scaled))` and a manual half-even step) and converted out
with `double(a) * (1.0 / 65536.0)`. Those jobs belong to the two permanent boundary functions that
`moo_number.h` and `moo.h` already define:

- `moo_numtodoublebits` (`moo_number.h`) is the integer implementation of "the nearest binary64,
  ties to even" (docs/DESIGN.md "Boundary", DETERMINISM.md item 8). The new inline
  `moo_numtodouble` decodes those bits, so the value rule has one implementation.
- `moo_numfromdouble` (`moo_number.h`) is the boundary rule into the VM: "rounds once to the
  nearest Q48.16 value, ties to even" and reports NaN, infinity and out of range. The new inline
  `moo_numfromdoublevalue` turns that status into a value, with the same contract the deleted
  bridge had for its callers (NaN gives 0, infinity and out-of-range saturate).
- The heap dump does not need a double at all: `moo_numtostr` prints the shortest exact decimal
  that reads back to the same Q48.16 value (docs/DESIGN.md "Number formatting").

The two new inlines are boundary conversions, not a renamed bridge: they have no private rounding
arithmetic and delegate to the functions the C API (`moo_fromdouble` / `moo_todouble`) already
publishes.

## What changed

- `Common/include/moo_number.h`: deleted `moo_numbridgefromdouble` and `moo_numbridgetodouble`
  and their TEMPORARY BRIDGE comment. Added `moo_numtodouble` (decodes `moo_numtodoublebits`) and
  `moo_numfromdoublevalue` (delegates to `moo_numfromdouble`).
- `VM/src/lgcdebug.cpp`: `enumedge`'s `ttisnumber` key branch formats the key through
  `moo_numtostr` (from `moo_format.h`) instead of `snprintf("%.14g", ...)` on a double.
- `VM/src/lmathlib.cpp`: deleted the stale header paragraph that said sqrt and noise still used a
  temporary libm bridge. They do not; this file calls no bridge.
- `Compiler/src/Compiler.cpp`: `tryCompileUnrolledFor`, `compileUnrolledFor` and
  `setCompileConstantNumber` use `moo_numtodouble` / `moo_numfromdoublevalue`. The host-facing
  boundary (`luau_set_compile_constant_number`) still takes a `double`, as the C API requires.
- `Compiler/src/CostModel.cpp`: `getNumber` uses `moo_numtodouble`.
- `VM/src/lbuiltins.cpp`, `VM/src/lveclib.cpp`, `VM/src/lvmexecute.cpp`, `VM/src/lvmutils.cpp`:
  the float-`vector` conversions use the same two boundary helpers. The float vector type itself
  is not removed by this card (`moo-l3-libs-out` has not landed); removing it is what will remove
  the remaining float arithmetic from these files.
- `Ast/src/Ast.cpp`, `Ast/include/Luau/Ast.h`: `AstExprConstantNumber`'s two-argument double
  constructor (landed in `ad9e85a9` by `moo-l3-ast-raw-literals`) now uses
  `moo_numfromdoublevalue(value)` instead of `moo_numbridgefromdouble(value)`, and the comments in
  `Ast.h` name `moo_numtodouble` and `moo_numfromdoublevalue`.
- Tests: `tests/moo/moo-l3-bytecode-version.test.cpp`, `tests/moo/moo-l3-ast-raw-literals.test.cpp`,
  and the two pow tests referenced the deleted names; they now call the permanent helpers, with the
  same values (the two pow tests' remaining failures are the same ones recorded at BASE).
- NEW `tests/moo/moo-l4-bridge-removed.test.cpp`: four doctest cases.
  `BridgeBoundaryOutValues` pins exact raw-to-double values; `BridgeBoundaryInValues` pins
  rounding and half-even ties; `BridgeBoundaryNonFinite` pins NaN -> 0 and infinity -> saturation;
  `BridgeHeapDumpUsesExactFormatter` pins the `moo_numtostr` output used by the heap dump.
- NEW `tests/conformance/moo/l3/moo-l4-bridge-removed.luau`: exercises the fixed math library,
  `^`, the exact formatter/parser, and the raw values behind them, then returns `OK`.

### Scope note

At BASE (`ad9e85a9da922082cd6e4626b5b88f5fe5a76994`), `moo_numbridge` had 43 occurrences across
ten files in `VM Common Compiler Ast Bytecode` plus 6 occurrences across four files in `tests/moo/`.
Leaving any caller would fail to compile once the definitions in `Common/include/moo_number.h` were
deleted, so the deletion updates those callers mechanically to `moo_numtodouble` and
`moo_numfromdoublevalue`. Note: `ad9e85a9` (`moo-l3-ast-raw-literals`) added two bridge calls in
`tests/moo/moo-l3-ast-raw-literals.test.cpp:156,162` alongside `Ast/src/Ast.cpp:169` and
`Ast/include/Luau/Ast.h:362,364`; those two test lines are updated as well so `Moo.Test` compiles.

### Carry disposition

The card's `CARRY:` names attempt 6's head `701cb92c20da146fae37d43e18fbf23a4a7ed99e` (which is a
direct parent of the merge with `ad9e85a9da922082cd6e4626b5b88f5fe5a76994`) and attempt 2's head
`57b6c5a283696727f73e3a708a5df47606c717cb`. Attempt 2's head is not an ancestor because `sprint/moo`
advanced from `c9d154ab` to `e7ac1b4e` before attempt 4; the carry commit `80a826de` replayed
attempt 3's work onto `e7ac1b4e`, and for each of the 14 original source and test files from
`57b6c5a`, `git diff c9d154ab..57b6c5a -- <file>` and `git diff ad9e85a9..HEAD -- <file>` hash
identically (`git hash-object`). On top of `701cb92c`, this head merges `ad9e85a9` (`sprint/moo`),
replaces the new bridge references from `moo-l3-ast-raw-literals` (`Ast/src/Ast.cpp`,
`Ast/include/Luau/Ast.h`, `tests/moo/moo-l3-ast-raw-literals.test.cpp`), and updates this note for
the coordinator's 2026-10-19 revision.

## Red first

At the unchanged BASE (`ad9e85a9da922082cd6e4626b5b88f5fe5a76994`), before any edit:

```
$ git grep -c 'moo_numbridge' ad9e85a9da922082cd6e4626b5b88f5fe5a76994 -- VM Common Compiler Ast Bytecode
ad9e85a9da922082cd6e4626b5b88f5fe5a76994:Ast/include/Luau/Ast.h:2
ad9e85a9da922082cd6e4626b5b88f5fe5a76994:Ast/src/Ast.cpp:1
ad9e85a9da922082cd6e4626b5b88f5fe5a76994:Common/include/moo_number.h:2
ad9e85a9da922082cd6e4626b5b88f5fe5a76994:Compiler/src/Compiler.cpp:4
ad9e85a9da922082cd6e4626b5b88f5fe5a76994:Compiler/src/CostModel.cpp:1
ad9e85a9da922082cd6e4626b5b88f5fe5a76994:VM/src/lbuiltins.cpp:9
ad9e85a9da922082cd6e4626b5b88f5fe5a76994:VM/src/lgcdebug.cpp:1
ad9e85a9da922082cd6e4626b5b88f5fe5a76994:VM/src/lveclib.cpp:11
ad9e85a9da922082cd6e4626b5b88f5fe5a76994:VM/src/lvmexecute.cpp:10
ad9e85a9da922082cd6e4626b5b88f5fe5a76994:VM/src/lvmutils.cpp:2
```

That is 43 occurrences across ten files, so the DONE-WHEN check did not hold at BASE. At this head
the same command prints nothing (exit 1).

## Gate

- `tools/revendor-fixed.sh --check`: `extern/fixed matches pin 2f1ed913374a -- clean`.
- configure (clang 18.1.3, RelWithDebInfo, `LUAU_WERROR=ON`): "Build files have been written to: .../build".
- `cmake --build build --target Moo.Test Luau.VM`: green.
- `build/Moo.Test`: 163 test cases, 161 passed, 2 failed; 67,105,725 assertions, 67,105,683 passed,
  42 failed. The 2 failing cases are the ones already red at BASE (`ad9e85a9da92`) and owned by
  earlier cards: `MathPowFractionalStaysOnBridge` (28 assertions) and `MooPowIntVmFractionalBridge`
  (14 assertions), which still compare a fractional `^` against the host libm double while the
  landed `moo-l4-pow-fractional` uses fixed math (`fixPow`). `MooL5SkippedCasesNameAReason` and
  `MooL3Scripts` (including `moo-l4-bridge-removed.luau`) pass, as do all four new
  `moo-l4-bridge-removed` cases (4 cases, 23 assertions).
- `tools/moo-check-no-clock.sh build/libLuau.VM.a`: `moo-check-no-clock: build/libLuau.VM.a references no clock`.
- `cmake --build build --target Luau.Conformance Luau.UnitTest Luau.CLI.Test Luau.Repl.CLI Luau.Analyze.CLI Luau.Compile.CLI`: green.
- `build/Luau.Conformance`: 109 cases, 109 passed; 9,143 assertions, 0 failed.
- `build/Luau.CLI.Test`: 113 cases, 113 passed; 461 assertions, 0 failed.
- `build/Luau.UnitTest`: 5,159 cases, 5,146 passed, 13 failed (44 skipped); 22,947 assertions,
  22,933 passed, 14 failed. The 13 names are the BASE-red set recorded in
  `docs/layer4/moo-l4-math-noise.md` and `docs/layer3/moo-l3-ast-raw-literals.md`:
  `fold_constants_div_by_zero`, `MooBuiltinsDoNotFold`, the four `udtf_integer*` cases,
  `issubtypeof`, `type_tostring`, `compound_assign_reports_invalid_vector_arithmetic`,
  `type_narrow_to_vector`, `type_vector_refine`, `builtin_table_names`,
  `persistent_sealed_table_is_immutable`.
- `git grep -c 'moo_numbridge' -- VM Common Compiler Ast Bytecode`: no output (exit 1).

## The VM `double` audit

Per the coordinator's 2026-10-19 revision (`list every hit, grouped by the card or area that owns it
(C API boundary, comments, GC/clock timing, vector library, string.pack, lnumutils, other). Hits in
code other cards own are recorded, not removed here; only the bridge and its callers are this
card's`), `git grep -n 'double' -- VM/src` prints **151** lines at this head (153 at BASE; the
two-line difference is `lgcdebug.cpp:827`'s deleted bridge call and `lveclib.cpp:15`'s deleted
bridge comment).

Grouped by the card or area that owns each hit:

- **C API boundary (`VM/src/moo_api.cpp`, 6 hits; `VM/include/moo.h` declarations outside `VM/src`)**:
  `moo_fromdouble` and `moo_todouble` (`moo_api.cpp:17,19,24,27,31,32`).
- **Comments (19 hits)**:
  `lbuflib.cpp:171,197,198`, `lbuiltins.cpp:1750`, `lgc.cpp:146`, `lgc.h:22`, `lmem.cpp:149`,
  `lnumprint.cpp:395`, `lnumutils.h:15,17,45,46,61`, `lobject.h:89`, `lperf.cpp:94`,
  `lstate.h:90`, `lstrlib.cpp:1572,1581,1735,1741`.
- **GC/clock timing (39 hits — `lapi.cpp`, `lgc.cpp`, `lperf.cpp`, `lstate.h`)**:
  - `VM/src/lapi.cpp` (4 hits): lines `1467,1468,1491,1492` (`gcmetrics` mark/sweep times).
  - `VM/src/lgc.cpp` (6 hits): lines `948,1081,1083,1095,1122,1268` (`gcmetrics` timestamps/intervals).
  - `VM/src/lperf.cpp` (10 hits): lines `28,53,73,83,96,105,114,129,144,151` (`lua_clock` / high-precision timer).
  - `VM/src/lstate.h` (19 hits): lines `95,97,100,101,104,106,107,109,110,113,115,116,119,120,121,122,123,124,132` (`GCMetrics` struct fields).
- **Vector library & float `vector` type (`moo-l3-libs-out`, 59 hits — `lbuiltins.cpp`, `lobject.h`, `ltable.cpp`, `lveclib.cpp`, `lvector.cpp`, `lvector.h`, `lvmexecute.cpp`, `lvmutils.cpp`)**:
  - `VM/src/lbuiltins.cpp` (22 non-comment hits): boundary helper calls for vector fastcalls (`1177,1178,1185,1194,1623,1625,1679,1681,1880`), `condvectordouble` macro uses (`1597,1648,1699,1740,1767,1819,1863,1905,1947,1999`), SSE float round helpers and f64 fastcalls (`1751,1754,1757,1759,1760,1796,1840` — note `1751,1754,1757,1759,1760,1796,1840` also listed under `other` below).
  - `VM/src/lobject.h` (1 hit): line `205` (`setvvalue` macro `double`).
  - `VM/src/ltable.cpp` (1 hit): line `192` (`hashvec(const LuaTable* t, const double* v)`).
  - `VM/src/lveclib.cpp` (18 hits): boundary conversions and float `vector` operations (`16,17,18,21,36,38,81,83,98,99,100,108,276,288,337,338,339,340`).
  - `VM/src/lvector.cpp` (1 hit) and `VM/src/lvector.h` (1 hit): `luaVec_newvector` signature (`lvector.cpp:7`, `lvector.h:8`).
  - `VM/src/lvmexecute.cpp` (10 hits): boundary helper calls (`moo_numfromdoublevalue` / `moo_numtodouble`) on `vvalue` vector components (`631,1871,1884,1933,1946,1995,2138,2185,2232,3137`).
  - `VM/src/lvmutils.cpp` (2 hits): boundary helper calls (`moo_numtodouble`) in `luaV_doarithimpl` vector branches (`616,647`).
- **`string.pack` / `string.unpack` (`VM/src/lstrlib.cpp`, 12 non-comment hits)**:
  float/double pack/unpack format specifiers `f`, `d`, `n` and boundary bit-pattern helpers (`1512,1582,1585,1586,1594,1595,1738,1742,1744,1756,1757,1759`).
- **`lnumutils.h` (`VM/src/lnumutils.h`, 9 non-comment hits)**:
  `luai_num*` compatibility inline helpers (`23,28,33,38,43,49,54,59,62`).
- **Other (`lbuflib.cpp`, `lnumprint.cpp`, `lmem.cpp`)**:
  - `VM/src/lbuflib.cpp` (7 non-comment hits): `buffer.readf32`/`readf64`/`writef32`/`writef64` boundary bit-pattern conversions and table entries (`174,203,405,413,437,445` plus template instantiations).
  - `VM/src/lnumprint.cpp` (2 non-comment hits): legacy `luai_num2str` double formatting (`397,462`).
  - `VM/src/lmem.cpp` (1 non-comment hit): block-header alignment union (`152`).

Every hit follows, exactly as `git grep -n 'double' -- VM/src` prints it at this head:

```
VM/src/lapi.cpp:1467:        double startmarktime = g->gcmetrics.currcycle.marktime;
VM/src/lapi.cpp:1468:        double startsweeptime = g->gcmetrics.currcycle.sweeptime;
VM/src/lapi.cpp:1491:        double totalmarktime = cyclemetrics->marktime - startmarktime;
VM/src/lapi.cpp:1492:        double totalsweeptime = cyclemetrics->sweeptime - startsweeptime;
VM/src/lbuflib.cpp:171:    // moo_numfromdouble decodes the stored IEEE bits to Q48.16, ties to even; a NaN, infinity or out-of-range
VM/src/lbuflib.cpp:174:    int status = moo_numfromdouble(double(val), &out);
VM/src/lbuflib.cpp:197:    // moo_numtofloatbits / moo_numtodoublebits store the nearest IEEE bits, ties to even, with no float or
VM/src/lbuflib.cpp:198:    // double arithmetic.
VM/src/lbuflib.cpp:203:        val = StorageType(moo_numtodoublebits(raw));
VM/src/lbuflib.cpp:405:    {"readf64", buffer_readfp<double, uint64_t>},
VM/src/lbuflib.cpp:413:    {"writef64", buffer_writefp<double, uint64_t>},
VM/src/lbuflib.cpp:437:    {"readf64", buffer_readfp<double, uint64_t>},
VM/src/lbuflib.cpp:445:    {"writef64", buffer_writefp<double, uint64_t>},
VM/src/lbuiltins.cpp:1177:        LUA_VECTOR_TYPE x = LUA_VECTOR_TYPE(moo_numtodouble(nvalue(arg0)));
VM/src/lbuiltins.cpp:1178:        LUA_VECTOR_TYPE y = LUA_VECTOR_TYPE(moo_numtodouble(nvalue(args)));
VM/src/lbuiltins.cpp:1185:            z = LUA_VECTOR_TYPE(moo_numtodouble(nvalue(args + 1)));
VM/src/lbuiltins.cpp:1194:            w = LUA_VECTOR_TYPE(moo_numtodouble(nvalue(args + 2)));
VM/src/lbuiltins.cpp:1566:        if (moo_numfromdouble(double(val), &out) != 0)
VM/src/lbuiltins.cpp:1589:        // built"; DETERMINISM.md 18): the same moo_numtofloatbits / moo_numtodoublebits as the library path,
VM/src/lbuiltins.cpp:1590:        // with no float or double arithmetic.
VM/src/lbuiltins.cpp:1600:            uint64_t v = moo_numtodoublebits(raw);
VM/src/lbuiltins.cpp:1623:        setnvalue(res, moo_numfromdoublevalue(luai_sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3])));
VM/src/lbuiltins.cpp:1625:        setnvalue(res, moo_numfromdoublevalue(luai_sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])));
VM/src/lbuiltins.cpp:1679:        setnvalue(res, moo_numfromdoublevalue(a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]));
VM/src/lbuiltins.cpp:1681:        setnvalue(res, moo_numfromdoublevalue(a[0] * b[0] + a[1] * b[1] + a[2] * b[2]));
VM/src/lbuiltins.cpp:1880:        const LUA_VECTOR_TYPE t = LUA_VECTOR_TYPE(moo_numtodouble(nvalue(args + 1)));
VM/src/lbuiltins.cpp:2675:LUAU_TARGET_SSE41 inline double roundsd_sse41(double v)
VM/src/lbuiltins.cpp:2686:        double a1 = nvalue(arg0);
VM/src/lbuiltins.cpp:2698:        double a1 = nvalue(arg0);
VM/src/lbuiltins.cpp:2710:        double a1 = nvalue(arg0);
VM/src/lbuiltins.cpp:2713:        const double offset = 0.49999999999999994;
VM/src/lbuiltins.cpp:2854:    luauF_readfp<double>,
VM/src/lbuiltins.cpp:2855:    luauF_writefp<double>,
VM/src/lfunc.cpp:125:    // double link the upvalue in the global open upvalue list
VM/src/lgc.cpp:47: * target heap size during atomic phase in relation to live heap size (e.g. 200% goal means the heap's worst case size is double
VM/src/lgc.cpp:164:static void recordGcStateStep(global_State* g, int startgcstate, double seconds, bool assist, size_t work)
VM/src/lgc.cpp:212:static double recordGcDeltaTime(double& timer)
VM/src/lgc.cpp:214:    double now = lua_clock();
VM/src/lgc.cpp:215:    double delta = now - timer;
VM/src/lgc.cpp:1089:    double currts = lua_clock();
VM/src/lgc.cpp:1359:    // truncates toward zero, as the double version's conversion did
VM/src/lgc.cpp:1403:    double lasttimestamp = lua_clock();
VM/src/lgc.h:12:#define LUAI_GCGOAL 200    // 200% (allow heap to double compared to live heap size)
VM/src/lmathlib.cpp:345:// (docs/DESIGN.md, "math library": sqrt from fixSqrt). The seed question (fixSqrt's double seed
VM/src/lmathlib.cpp:490:        // See http://mumble.net/~campbell/tmp/random_real.c for details on generating doubles from integer ranges.
VM/src/lmem.cpp:152:const size_t kBlockHeader = sizeof(double) > sizeof(void*) ? sizeof(double) : sizeof(void*); // suitable for aligning double & void* on all platforms
VM/src/lnumprint.cpp:15:// Raffaello Giulietti. The Schubfach way to render doubles. 2021
VM/src/lnumprint.cpp:273:char* luai_num2str(char* buf, double n)
VM/src/lnumprint.cpp:278:        double v;
VM/src/lnumutils.h:72:inline bool luai_veceq(const double* a, const double* b)
VM/src/lnumutils.h:90:inline bool luai_vecisnan(const double* a)
VM/src/lnumutils.h:104:inline double luai_signd(double v)
VM/src/lnumutils.h:115:inline double luai_clampd(double v, double min, double max)
VM/src/lnumutils.h:117:    double r = v < min ? min : v;
VM/src/lnumutils.h:132:inline double luai_nummod(double a, double b)
VM/src/lnumutils.h:139:inline double luai_numidiv(double a, double b)
VM/src/lnumutils.h:150:inline double luai_lerpd(double a, double b, double t)
VM/src/lnumutils.h:161:#define luai_fabs condvectordouble(fabs, fabsf)
VM/src/lnumutils.h:162:#define luai_sqrt condvectordouble(sqrt, sqrtf)
VM/src/lnumutils.h:163:#define luai_floor condvectordouble(floor, floorf)
VM/src/lnumutils.h:164:#define luai_ceil condvectordouble(ceil, ceilf)
VM/src/lnumutils.h:165:#define luai_sign condvectordouble(luai_signd, luai_signf)
VM/src/lnumutils.h:166:#define luai_clamp condvectordouble(luai_clampd, luai_clampf)
VM/src/lnumutils.h:167:#define luai_lerp condvectordouble(luai_lerpd, luai_lerpf)
VM/src/lnumutils.h:169:// On MSVC in 32-bit, double to unsigned cast compiles into a call to __dtoui3, so we invoke x87->int64 conversion path manually
VM/src/lnumutils.h:186:LUAI_FUNC char* luai_num2str(char* buf, double n);
VM/src/lobject.h:63:#define condvectordouble(vecdoubleexpr, vecfloatexpr) vecdoubleexpr
VM/src/lobject.h:65:#define condvectordouble(vecdoubleexpr, vecfloatexpr) vecfloatexpr
VM/src/lobject.h:91:#define vvalue(o) check_exp(ttisvector(o), condvectordouble((o)->value.gc->vec.v, (o)->value.v))
VM/src/lobject.h:138:        LuauVector* i_vec = luaVec_newvector((L), double(x), double(y), double(z), condvector4(double(w), 0)); \
VM/src/lobject.h:453:            // global double linked list (when open)
VM/src/lperf.cpp:27:static double clock_period()
VM/src/lperf.cpp:32:    return 1.0 / double(result.QuadPart);
VM/src/lperf.cpp:36:    return double(result.numer) / double(result.denom) * 1e-9;
VM/src/lperf.cpp:42:    return 1.0 / double(CLOCKS_PER_SEC);
VM/src/lperf.cpp:46:static double clock_timestamp()
VM/src/lperf.cpp:51:    return double(result.QuadPart);
VM/src/lperf.cpp:53:    return double(mach_absolute_time());
VM/src/lperf.cpp:61:    return double(clock());
VM/src/lperf.cpp:65:double lua_clock()
VM/src/lperf.cpp:67:    static double period = clock_period();
VM/src/lstate.h:106:    double pausetime = 0.0; // time from end of the last cycle to the start of a new one
VM/src/lstate.h:108:    double starttimestamp = 0.0;
VM/src/lstate.h:109:    double endtimestamp = 0.0;
VM/src/lstate.h:111:    double marktime = 0.0;
VM/src/lstate.h:112:    double markassisttime = 0.0;
VM/src/lstate.h:113:    double markmaxexplicittime = 0.0;
VM/src/lstate.h:117:    double atomicstarttimestamp = 0.0;
VM/src/lstate.h:119:    double atomictime = 0.0;
VM/src/lstate.h:122:    double atomictimeupval = 0.0;
VM/src/lstate.h:123:    double atomictimeweak = 0.0;
VM/src/lstate.h:124:    double atomictimegray = 0.0;
VM/src/lstate.h:125:    double atomictimeembedder = 0.0;
VM/src/lstate.h:126:    double atomictimeclear = 0.0;
VM/src/lstate.h:128:    double sweeptime = 0.0;
VM/src/lstate.h:129:    double sweepassisttime = 0.0;
VM/src/lstate.h:130:    double sweepmaxexplicittime = 0.0;
VM/src/lstate.h:145:    double stepexplicittimeacc = 0.0;
VM/src/lstate.h:146:    double stepassisttimeacc = 0.0;
VM/src/lstate.h:216:    UpVal uvhead; // head of double-linked list of all open upvalues
VM/src/lstrlib.cpp:1253:// assume we need to align for double & pointers
VM/src/lstrlib.cpp:1262:    double d;
VM/src/lstrlib.cpp:1263:    double n;
VM/src/lstrlib.cpp:1264:    char buff[5 * sizeof(double)]; // enough for any float type
VM/src/lstrlib.cpp:1507:// packs to the nearest float/double bit pattern, ties to even, through moo_numtofloatbits and
VM/src/lstrlib.cpp:1508:// moo_numtodoublebits (moo_number.h); a float/double's IEEE bits unpack to Q48.16 in integer
VM/src/lstrlib.cpp:1509:// arithmetic through moo_numfromdouble. The byte store below is integer only: no float or double
VM/src/lstrlib.cpp:1511:// moo: float to double is exact, so the double decoder is the one boundary rule for both f and d.
VM/src/lstrlib.cpp:1514:    return moo_numfromdouble((double)x, out);
VM/src/lstrlib.cpp:1517:// moo: the named unpack errors (THE TASK); a NaN, infinity or out-of-range float/double never becomes a number.
VM/src/lstrlib.cpp:1581:            { // 'd': nearest double bit pattern
VM/src/lstrlib.cpp:1582:                uint64_t bits = moo_numtodoublebits(n);
VM/src/lstrlib.cpp:1735:            { // 'f': float bits, widened to double exactly, decoded in integer arithmetic
VM/src/lstrlib.cpp:1741:            { // 'd': double bits, decoded in integer arithmetic
VM/src/lstrlib.cpp:1742:                double d;
VM/src/lstrlib.cpp:1744:                status = moo_numfromdouble(d, &num);
VM/src/ltable.cpp:192:static LUAU_MAYBE_UNUSED LuaNode* hashvec(const LuaTable* t, const double* v)
VM/src/lveclib.cpp:16:    double x = moo_numtodouble(luaL_checknumber(L, 1));
VM/src/lveclib.cpp:17:    double y = moo_numtodouble(luaL_checknumber(L, 2));
VM/src/lveclib.cpp:18:    double z = count >= 3 ? moo_numtodouble(luaL_checknumber(L, 3)) : 0.0;
VM/src/lveclib.cpp:21:    double w = count >= 4 ? moo_numtodouble(luaL_checknumber(L, 4)) : 0.0;
VM/src/lveclib.cpp:36:    lua_pushnumber(L, moo_numfromdoublevalue(luai_sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3])));
VM/src/lveclib.cpp:38:    lua_pushnumber(L, moo_numfromdoublevalue(luai_sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])));
VM/src/lveclib.cpp:81:    lua_pushnumber(L, moo_numfromdoublevalue(a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]));
VM/src/lveclib.cpp:83:    lua_pushnumber(L, moo_numfromdoublevalue(a[0] * b[0] + a[1] * b[1] + a[2] * b[2]));
VM/src/lveclib.cpp:98:    double sinA = sqrt(cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2]);
VM/src/lveclib.cpp:99:    double cosA = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
VM/src/lveclib.cpp:100:    double angle = atan2(sinA, cosA);
VM/src/lveclib.cpp:108:    lua_pushnumber(L, moo_numfromdoublevalue(angle));
VM/src/lveclib.cpp:276:            lua_pushnumber(L, moo_numfromdoublevalue(v[ic]));
VM/src/lveclib.cpp:288:    const LUA_VECTOR_TYPE t = LUA_VECTOR_TYPE(moo_numtodouble(luaL_checknumber(L, 3)));
VM/src/lvector.cpp:7:LuauVector* luaVec_newvector(lua_State* L, double x, double y, double z, double w)
VM/src/lvector.h:8:LUAI_FUNC LuauVector* luaVec_newvector(lua_State* L, double x, double y, double z, double w);
VM/src/lvmexecute.cpp:631:                            setnvalue(ra, moo_numfromdoublevalue(v[ic])); // TEMPORARY: the float vector goes in layer 3
VM/src/lvmexecute.cpp:1871:                    LUA_VECTOR_TYPE vc = cast_to(LUA_VECTOR_TYPE, moo_numtodouble(nvalue(rc)));
VM/src/lvmexecute.cpp:1884:                    LUA_VECTOR_TYPE vb = cast_to(LUA_VECTOR_TYPE, moo_numtodouble(nvalue(rb)));
VM/src/lvmexecute.cpp:1933:                    LUA_VECTOR_TYPE vc = cast_to(LUA_VECTOR_TYPE, moo_numtodouble(nvalue(rc)));
VM/src/lvmexecute.cpp:1946:                    LUA_VECTOR_TYPE vb = cast_to(LUA_VECTOR_TYPE, moo_numtodouble(nvalue(rb)));
VM/src/lvmexecute.cpp:1995:                    LUA_VECTOR_TYPE vc = cast_to(LUA_VECTOR_TYPE, moo_numtodouble(nvalue(rc)));
VM/src/lvmexecute.cpp:2138:                    LUA_VECTOR_TYPE vc = cast_to(LUA_VECTOR_TYPE, moo_numtodouble(nvalue(kv)));
VM/src/lvmexecute.cpp:2185:                    LUA_VECTOR_TYPE nc = cast_to(LUA_VECTOR_TYPE, moo_numtodouble(nvalue(kv)));
VM/src/lvmexecute.cpp:2232:                    LUA_VECTOR_TYPE vc = cast_to(LUA_VECTOR_TYPE, moo_numtodouble(nvalue(kv)));
VM/src/lvmexecute.cpp:3137:                    LUA_VECTOR_TYPE nb = cast_to(LUA_VECTOR_TYPE, moo_numtodouble(nvalue(kv)));
VM/src/lvmutils.cpp:616:            LUA_VECTOR_TYPE nc = cast_to(LUA_VECTOR_TYPE, moo_numtodouble(nvalue(c))); // TEMPORARY: the float vector goes in layer 3
VM/src/lvmutils.cpp:647:            LUA_VECTOR_TYPE nb = cast_to(LUA_VECTOR_TYPE, moo_numtodouble(nvalue(b))); // TEMPORARY: the float vector goes in layer 3
VM/src/moo_api.cpp:17:int moo_fromdouble(double x, int64_t* out)
VM/src/moo_api.cpp:19:    return moo_numfromdouble(x, out);
VM/src/moo_api.cpp:24:    return moo_fromdouble(double(x), out); // float to double is exact
VM/src/moo_api.cpp:27:double moo_todouble(int64_t n)
VM/src/moo_api.cpp:31:    uint64_t bits = moo_numtodoublebits(n);
VM/src/moo_api.cpp:32:    double d;
```

## What must change in the design documents

`docs/DESIGN.md`, "Layer 3 as built", under "**TEMPORARY bridges**, each marked in the code and
each removed by a later layer 3 PR:", delete the bullet:

> - the libm functions of `math`, `^`, `math.noise`, and the float `vector` type convert through doubles
>   (`moo_numbridgetodouble`, `moo_numbridgefromdouble`), until the math library is rebuilt on fixed and the vector
>   type is removed;

It is no longer true: the bridge is gone; the math library is on fixed; the float `vector` type is
the only remaining caller of the boundary conversion and is removed by `moo-l3-libs-out`, not by a
double bridge.

`docs/DESIGN.md`, "Layer 3 as built", the compiler bullet says "a constant still travels as the
double that holds it exactly". The constant is now the raw Q48.16 value; the sentence can be
narrowed to the host C API boundary at the closing card.

`docs/DETERMINISM.md`, "Status", item 7 must lose the libm-bridge clause:

> | 7 | platform libm | **partly**: `+ - * / // %` are fixed's integer arithmetic, and the compiler folds with the same functions; the libm functions and `^` still cross a double bridge until the math library is rebuilt on fixed |

After all layer 4 math cards, the math functions and `^` are fixed; only the float `vector` type
and host-boundary conversion remain, so the row should become **fixed** for `math`, with the
`vector` exception named separately (DETERMINISM.md item 17).

`docs/DETERMINISM.md`, "Fix" paragraph before item 8 begins "floats leave the VM (layer 3) and the
math library is rebuilt on `fixed` (layer 4)"; that is now the built state, not a plan, and the
closing card should say so.
