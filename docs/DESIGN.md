# moo design

moo is Luau with two changes: everything in the language is deterministic, and `number` is
Q48.16 fixed point. Floats and doubles are outlawed inside the VM. Everything else (syntax,
tools, the C API) stays as close to Luau as possible.

**Status.** Layers 1-5 are built. Layer 3b is built except the expression-temporary reuse in the
compiler, which is not built, and awaits its lock. The conformance record is the program-by-program
matrix in DETERMINISM.md, "Status". The sections "Layer 3 as built" and "Layer 3b as built" below
are the state of `main`. Everything else here is design.

The determinism guarantee, and every source of non-determinism found in Luau 0.741 with its
file and line, is in [DETERMINISM.md](DETERMINISM.md). This document says what replaces them.

## Layer 3 as built

- **The number is fixed.** `lua_Number` is `int64_t`, the raw Q48.16 value; `TValue` stores it where Luau stored the
  double. Every operator goes through `Common/include/moo_number.h` (add, sub, negate exact and saturating; `fixMul`;
  `fixDiv`; exact floor division and remainder), behind Luau's `luai_num*` seams. `/`, `//` and `%` by zero raise
  `division by zero`. The domain is [-INT64_MAX, INT64_MAX] raw. A checked build (`MOO_OVERFLOW_ERRORS`, on with
  assertions) raises `number overflow in '<op>'` where release saturates (`moo_num*saturates`, `luai_numnosat`, raised
  in `luaV_doarithimpl`).
- **The C API boundary.** `lua_pushnumber`, `lua_tonumber`, `luaL_checknumber` and the rest take and return the raw value.
  In C++ a `double`, `float` or `int` passed to `lua_pushnumber` is a compile error; hosts convert with
  `moo_fromdouble` / `moo_todouble` / `moo_tofloat` (moo.h). Building with `-DMOO_NUMBER_PROBE` makes `lua_Number` a
  struct, so every implicit use of a number is a compile error: that is how each site was found.
- **The compiler** folds number arithmetic with the same `moo_number.h` operations (a constant is the raw Q48.16
  value, and a folded constant is the exact value the same expression computes at run time, at every magnitude); it
  does not fold division by zero, `^`, or any builtin call or `math` constant, or, in a checked build, an operation
  that saturates. The bytecode-graph constant propagation (`Bytecode/include/Luau/Sccp.h`
  `evaluateNumberBinaryOp`) folds the same way on the raw value. The compiler does not recognise `vector.*` or
  `CompileOptions::vectorLib` / `vectorCtor` as builtins and never folds or emits a vector constant
  (`Compiler/src/Builtins.cpp`, `Compiler/src/ConstantFolding.cpp`, `Compiler/src/Compiler.cpp`).
- **The moo bytecode version** (`Common/include/Luau/Bytecode.h`, "moo bytecode versions"): a moo blob starts with
  `0x80 | moo version` (1), then the Luau version its format follows. A number constant is the raw Q48.16 value, 8
  bytes little-endian. The VM refuses Luau bytecode (`this is Luau bytecode (version N), not moo bytecode`), an
  unknown moo version, an integer constant (`moo bytecode has no integer constant`) and `INT64_MIN`, each by name.
  The bytecode dump prints numbers through `moo_format.h`.
- **Native code generation** is excluded (below).
- **The suite says what it skips.** Every skipped doctest case and every moved conformance
  script states its reason with `moo:`; `tools/moo-check-skips.sh` lists them and fails when a
  reason is missing or names a replacement that does not exist.
- **The determinism suite runs on Windows.** The nightly builds `Moo.Test` (the determinism
  suite and the math oracle) on `windows-latest` with MSVC and with clang-cl and runs it; the
  job fails when either the effects hash or the state hash differs from
  `tests/determinism/expected.txt`. No frozen hash changes.
- **TEMPORARY bridges.** None remain. `tools/moo-check-bridges.sh` reports 0 bridge calls, 0 TEMPORARY markers and 0
  definitions in the built sources. The last bridge was the math library and `^` on libm, rebuilt on `fixed` in layer
  4 (DESIGN.md, "Layers", item 4); the float `vector` type and its bytecode constant left in layer 3, and the boundary,
  `buffer` and `string.pack` floats are exact integer conversions (below).
- **Literals are exact** (`Common/include/moo_parse.h`): the parser reads a decimal literal as the exact rational it
  denotes and rounds once to Q48.16, ties to even, with no libc and no locale; exponent notation stays; `0x` and `0b`
  literals are integers. A literal beyond the range is the compile error `Number literal out of range`; a nonzero
  literal that rounds to 0 is the lint warning "below the resolution". The AST and the bytecode carry the exact raw
  Q48.16 value, over the whole range.
- **Formatting and tonumber are exact** (`Common/include/moo_format.h`): `tostring` prints the shortest decimal that
  reads back to exactly the same value (`0.1 + 0.2` prints `0.3`, because both read as 19661/65536), integers with no
  fraction; `string.format`'s `%f`, `%e`, `%g`, `%E` and `%G` follow C's flags, width and precision on the exact decimal
  expansion, rounding half to even, with no libc. `tonumber` and string coercion use the literal parser, with spaces
  and one sign around it; out of range is not a number. A libFuzzer target feeds arbitrary strings to the parser and
  arbitrary raw values through the formatter and requires `parse(format(x)) == x` (`fuzz/moo_arith.cpp`,
  `fuzz/number.cpp`, `fuzz/format.cpp`).
- **Numeric `for` is exact** (`tla/NumericFor.tla`): `LOP_FORNPREP` computes the trip count once, in integer
  arithmetic on the raw values (`moo_numfortrips`), and keeps the iterations left in the limit's register; the index
  never steps past the limit and never saturates. `for i = 0, 1, 0.1` runs 10 times (0.1 is 6554/65536). A zero step
  is the named error `'for' step is zero`. The compiler's loop unrolling already counts small integer loops exactly.
- **Exact already:** `math.abs`, `floor`, `ceil`, `min`, `max`, `clamp`, `sign`, `round` and `random` (16 bits per draw),
  table keys (integer-valued numbers use the array part; numbers hash by their raw value), and every integer
  conversion (toward zero, saturating at the int range, so no undefined float-to-int cast). The SSE4.1 fastcalls of
  `floor`, `ceil` and `round`, chosen by CPUID, are gone: one code path everywhere.
- **Functions that need an integer** (`lua_tointegerx`, `lua_tounsignedx`, `luaL_checkinteger`, `luaL_optinteger`,
  `luaL_checkunsigned`, and every integer-taking bytecode fastcall in `VM/src/lbuiltins.cpp` via `luai_numisint`)
  accept only integer-valued numbers. A fraction raises `number has no integer representation`, as in Lua 5.3,
  identically at `-O0`, `-O1` and `-O2`.
- **Checked against `fixed`'s C** (`tests/moo/moo-l3-oracle-harness.test.cpp`, doctest suite `moo-oracle`, oracle
  `tests/moo/moo_oracle_fixed.c`): every operator, bit for bit, on `moo_number.h`, the VM, the compiler's folding and
  the K-operand opcodes, over the edge set crossed with itself and 2^16 seeded random pairs (`MOO_ORACLE_N` raises it
  for the nightly).
- **The float `vector` is out.** The library, the type, the bytecode constant and the C API are refused by name
  (`moo-l3-libs-out`, `moo-l3-vector-compiler-out`, `moo-l3-vector-constant-refused`, `moo-l3-vector-api-out`,
  `moo-l3-vector-vm-out`); `LUA_TVECTOR` stays unused in the tag enum so upstream merges stay mechanical.
- **The merge checklist is built.** `docs/PATCHES.md` lists every upstream file layer 3 modifies or deletes with the
  DESIGN.md or DETERMINISM.md section it implements, and `tools/moo-check-patches.sh` refuses a change that is not in
  it.

## Layer 3b as built

The layer's record is the notes in `docs/layer3b/`; each names its files, tests and re-pins.

- **vec3 is a GC object.** `LUA_TVEC3` (14) follows `LUA_TOBJECT`; a vec3 is `MooVec3` (`VM/src/moo_vec3.h`), the
  16-byte header and three `fixed_t`, 40 bytes, immutable. `==`, `rawequal` and table keys compare and hash the three
  raw components; `tostring` prints `vec3(x, y, z)` with the exact formatter; the GC treats it as a leaf and never
  clears it from a weak table. `LUA_TVECTOR` stays unused. `cleartable` turns the key of an emptied entry into
  `LUA_TDEADKEY` before the sweep frees its object, because value equality reads the key's object
  (`docs/layer3b/moo-v2-l3b-vec3-object.md`, decision 4).
- **vec3 operations.** `+`, `-`, `*` and unary `-` follow the number's saturation and checked-build error: `+` and `-`
  are fixed's `fixVecAdd`/`fixVecSub`, which under `FIX_SATURATE` saturate per component (fixed #60) by the same
  rule, and to the same bits, as `moo_numadd`/`moo_numsub`; `*` is `fixVecMul` and `fixMulSV`; unary `-` is `fixVecNeg`. `vec3 == vec3` is component equality. `v.x`,
  `v.y`, `v.z` (and `X`, `Y`, `Z`) read a component; assigning one is the index error. The `vec3` library (`new`,
  `dot`, `cross`, `length`, `normalize`, aliases `create` and `magnitude`) calls fixed's functions directly and is
  opened by `luaL_openlibs`.
- **quat is a GC object.** `LUA_TQUAT` (15) follows `LUA_TVEC3`; a quat is `MooQuat` (`VM/src/moo_quat.h`), the
  16-byte header and a `fixQuat` (a vec3 and a scalar), 48 bytes, immutable. `==`, `rawequal` and table keys compare
  and hash the four raw components; `tostring` prints `quat(x, y, z, s)` with the exact formatter; the GC treats it as
  a leaf and never clears it from a weak table. `TKey::tt` is 5 bits and `TKey::next` 27 signed bits, so
  `LUA_TDEADKEY` fits; `sizeof(TKey)` does not change.
- **quat operations.** `quat * quat` is `fixMulQuat` and `quat * vec3` is `fixRotateVector`, fixed's compound
  functions verbatim, so a checked build raises no named error for them; every other operator on a quat is the
  ordinary arithmetic error. `==` is component equality. `q.x`, `q.y`, `q.z`, `q.w` (and upper case) read the
  components, `w` being fixed's `s`. The `quat` library is `new` (omitted components are the identity's, no
  normalization), `conjugate` (`fixConjugate`) and `nlerp` (`fixNLerp`; an alpha outside [0, 1] is the named error
  `alpha must be in [0, 1]`); `quat.slerp` is the named error `quat.slerp waits for fixed's slerp` until fixed has one.
- **transform is a GC object.** `LUA_TTRANSFORM` (16) follows `LUA_TQUAT`, so `LUA_TDEADKEY` and `LUA_T_COUNT` are 17;
  a transform is `MooTransform` (`VM/src/moo_transform.h`), the 16-byte header and a `fixTransform` (position `p`,
  rotation `q`), 72 bytes, immutable. `==`, `rawequal` and table keys compare and hash the seven raw components;
  `tostring` prints `transform(vec3(px, py, pz), quat(qx, qy, qz, qs))`; the GC treats it as a leaf and never clears it
  from a weak table.
- **transform operations.** `transform * transform` composes (`fixMulTransforms`) and `transform * vec3` transforms a
  point (`fixTransformPoint`); `==` is component equality; `t.p` and `t.q` read the parts as a new vec3 and a new quat.
  The `transform` library (`new`, `inverse`, `point`, `direction`, `invpoint`, `invdirection`) is fixed's functions
  with the one position add saturating (`VM/src/moo_transformlib.cpp`; "Saturation, stated precisely").
  `transform.new` takes omitted parts from the identity. `fixInvMulTransforms` is not a library function: a script
  writes `transform.inverse(a) * b`, which is not bit-identical to it.
- **Every operation allocates one new object.** The representation table's cost of one allocation per operation is
  now the implementation; one `vec3 + vec3` adds exactly 40 bytes to `totalbytes`.
- **The result is stored before the GC check.** A GC step in the propagate phase can shrink the running thread's
  stack (`lgc.cpp`, `shrinkstack`), so a register pointer taken before `luaC_checkGC` is stale after it. Every vec3,
  quat and transform operator stores its new object first and then checks, as upstream's `LOP_NEWTABLE` does (commit
  `8d94c4b1` "vec3, quat: store an operator's result before the GC check", and `e896313c` for transform).
  `tests/moo/moo-v2-l3b-gc-stack.test.cpp` grows the stack, returns, keeps the collector due and runs the operators
  under an allocator that moves every reallocation; it was red on the old order.
- **The C API.** `moo_pushvec3`/`moo_tovec3`, `moo_pushquat`/`moo_toquat` and `moo_pushtransform`/`moo_totransform`
  (`VM/include/lualib.h`) carry raw Q48.16 components across the C API, in fixed's order (x, y, z, s for a quat;
  px, py, pz, qx, qy, qz, qs for a transform). Each push allocates one object; each `moo_to*` returns 0 when the value
  is not of its type. `lua_isvec3` tests the tag. `lua_topointer` returns `NULL` for a vec3, a quat or a transform:
  two equal values are one value, so no address is exposed.
- **The analyzer** declares `vec3`, `quat` and `transform` as builtin extern types (`declare extern type ... with`,
  `Analysis/src/EmbeddedBuiltinDefinitions.cpp`, `kBuiltinDefinitionMooValueTypesSrc`), with read-only components and
  exactly the operators `luaV_doarithimpl` implements as metamethods, and the three libraries with the VM's
  signatures. `quat.slerp` is not declared while the VM refuses it. `tests/MooAnalyzer.test.cpp` (suite
  `MooAnalyzerValueTypes`) accepts everything the VM implements and reports 15 refused operations as type errors.
- **The 3b oracle.** `tests/moo/moo-v2-l3b-conformance.test.cpp` (suite `moo-v2-l3b-conformance`, case
  `Layer3bOracle`) runs all 22 operations of the three types in moo, through script functions called through the C API
  so nothing folds, and in C on the same operands: the layer 3 oracle's definitions (`tests/moo/moo_oracle_fixed.c`)
  for moo's own operators, fixed's functions for the compound ones, and both regimes of the transform position add.
  2^14 cases per operation; `MOO_ORACLE_3B_N` sets the count, and the nightly (`.github/workflows/nightly.yml`, Linux,
  macOS and Windows jobs) runs it at 2^22. On the studio (AppleClang arm64, RelWithDebInfo):

      moo-3b-oracle ops=22 cases=360448 per-op=16384 mismatches=0 transform-also-fixed=48170 transform-saturated=982 fixdot-wraps=0
      MOO_ORACLE_3B_N=4194304:
      moo-3b-oracle ops=22 cases=92274688 per-op=4194304 mismatches=0 transform-also-fixed=12326368 transform-saturated=256544 fixdot-wraps=131

- **The 3b oracle's reversed witness.** `Layer3bOracleGoesRedOnAPlantedWrongOp` (same suite) plants a wrong operation
  in the oracle's script functions and requires the gate to go red on that operation and on no other: `vec3 + vec3`
  and `vec3.dot` one raw unit off, `quat.nlerp` with its endpoints swapped, `transform.direction` as `invdirection`,
  and `transform.point` without its translation. Unplanted, the same 256-case run has no mismatch. A wrong operation
  planted in the VM itself (`vec3.cross` as `fixCross(b, a)` in `VM/src/moo_vec3lib.cpp`, run by hand and reverted)
  turned `Layer3bOracle` red, as `transform.point` with a plain add did before it
  (`docs/layer3b/moo-v2-l3b-conformance.md`).
- **Against fixed3d.** fixed3d vendors the same `fixed`, and its `b3*` vector, quaternion and transform functions
  forward to fixed's. `tools/moo-fixed3d-compare.sh` clones fixed3d at its pin (`071a3f6`, which vendors fixed
  `4a70c64` like moo), refuses unless fixed3d's `fixed` headers are identical to `extern/fixed`, builds
  `tools/moo-fixed3d-compare/compare.cpp` with `BOX3D_FIXED_SATURATE` against moo's libraries and runs 2^16 cases per
  operation; any difference exits 1. The `full` job of `ci.yml` runs it on every pull request:

      fixed3d-compare BOX3D_FIXED_SATURATE ops=22 cases=1441792 differ=0

  The first comparison, at fixed3d `29d5544` and moo before fixed #59-#62 were vendored (#40), had 136654 of 1441792
  cases differ, from two causes only. Rebuilt with `-DFIX_SATURATE` as well, the only differences left are
  moo's saturating `+` and `-` against fixed's plain int64 adds: `vec3 + vec3` (13472), `vec3 - vec3` (13858), and
  `transform * transform`, `transform.point` (13472 each) and `transform.invpoint` (13858) with an identity rotation
  and full-range positions. Each is a case where fixed's add overflows int64. The other 17 operations differ on 0
  cases, and inside the range moo and fixed3d agree bit for bit (`docs/layer3b/moo-v2-l3b-conformance.md`).
- **Upstream finding: fixed3d's `BOX3D_FIXED_SATURATE` does not reach fixed's inline `fixMul`.** `fixed_compat.h`
  includes `fixed/fixed.h` and only then defines `FIX_SATURATE` from `BOX3D_FIXED_SATURATE`, so `fixMul`'s
  `#if defined( FIX_SATURATE )` is already compiled out. A fixed3d build "with `BOX3D_FIXED_SATURATE`" wraps `fixMul`,
  `fixMulSV`, `fixCross` and `fixVecMul` as the default build does (`vec3 * number` differs on 32788 of 65536
  full-range cases, `vec3.cross` on 35734). Fixed upstream: at fixed3d `071a3f6` `fixed_compat.h` defines
  `FIX_SATURATE` from `BOX3D_FIXED_SATURATE` before it includes `fixed/fixed.h`, and refuses to build when
  `fixed/fixed.h` was included earlier without it.
- **Upstream finding: `fixDot` wraps at the top of the range.** `fixDotRaw` sums three 126-bit products in 128 bits
  through unsigned arithmetic, so a sum of magnitude 2^127 or more wraps and `fixDot` returns the wrong sign:
  `vec3.dot(v, v)` for `v = vec3.new(2^47 - 1, 2^47 - 1, 2^47 - 1)` is the most negative number where the exact result
  saturates to the most positive. The result is still deterministic (unsigned wrap is defined), and `fixLength` is
  unaffected. In the 2^22 run 131 of 4194304 random dot cases wrapped. Fixed upstream (fixed #59): at pin `4a70c64`
  `fixDot` saturates with the true sign, the oracle counts 0 wraps, and `Layer3bFixDotSaturatesAtTheRangeEnd` holds it.
- **Not built.** Expression-temporary reuse in the compiler ("Representations costed"). `quat.slerp` waits for
  `fixed`. (The `fixCross`, `fixRotateVector` and `fixMulAdd` sums that were owed upstream saturate under
  `FIX_SATURATE` since fixed #60-#62; "Saturation, stated precisely".)

## Representation

There is one number type. Its value is the raw signed 64-bit Q48.16 integer, `fixed_t`
(48 integer bits, 16 fraction bits), stored in the `TValue` where Luau stores the double.
`TValue` stays 16 bytes and the `LUA_TNUMBER` tag is unchanged.

- Resolution: 2^-16 (about 0.0000153).
- Range: -2^47 to 2^47 - 2^-16 (about ±1.4 × 10^14).
- There is no negative zero, no NaN and no infinity.

## Semantics

Arithmetic is the C library [`fixed`](https://github.com/mas-bandwidth/fixed), pinned at
commit `4a70c64eaedf58cb6363ad7a3a48387d2ff15502`, the commit fixed3d vendors. moo vendors it
the same way fixed3d does: a generated copy under `extern/fixed`, written by a revendor script,
with a CI check that fails on drift. A change to the arithmetic is made upstream in `fixed`
and arrives here by moving the pin.

What moo takes from `fixed` at that pin, and how each operator maps:

| moo | fixed | rounding |
|---|---|---|
| `a + b`, `a - b` | int64 add/sub, saturating (moo wrapper) | exact |
| `a * b` | `fixMul` with `FIX_SATURATE` | round half up |
| `a / b` | `fixDiv` | toward zero |
| `a // b` | integer floor of the exact quotient (moo) | floor |
| `a % b` | raw int64 floor remainder (moo); exact, since both operands share the 2^16 scale | exact |
| `-a` | negate, saturating | exact |
| comparisons | int64 compare | exact |

`fixMul` rounds half up and `fixDiv` truncates toward zero; moo keeps fixed's rules rather than
inventing its own, so a moo program and the same computation in fixed's C give the same bits.
Literals and the boundary use ties-to-even (below) because they convert *from* another number
system.

## Overflow

Saturates, never wraps: a result above the range is the largest value, below it the smallest.
`fixed` saturates multiplication only when built with `FIX_SATURATE`, and its add is a plain
int64 add, so moo builds `fixed` with `FIX_SATURATE` and wraps `+`, `-` and unary `-` in
saturating versions.

Checked builds raise the named error `number overflow in '<op>'` at the operation, for `+`, `-`,
`*`, `/` and `//` (`^` joins them when it is rebuilt on fixed). A build is checked when
assertions are on (no `NDEBUG`, or `LUAU_ENABLE_ASSERT`), or when it sets `MOO_OVERFLOW_ERRORS=ON`
(CMake) or `overflow=on` (make). Unary `-` and `%` are exact on the domain and never saturate. An
exact result at the edge (`MAX * 1`) is not overflow. The compiler does not fold an operation
that saturates in a checked build. Saturation is the release behaviour, so the same program never
gives different *values* in checked and release builds; it either gives the same value or stops
with the named error in a checked build.

## Literals

- Decimal literals (`12`, `0.1`, `3.25`) are read as an exact rational number and rounded
  **once** to the nearest Q48.16 value, ties to even. No `strtod`; the parser is integer
  arithmetic in moo.
- Hexadecimal (`0xff`) and binary (`0b1010`) literals are integers, exact.
- **Exponent notation stays** (`1e3`, `2.5e-3`, `1E+2`): it is Luau syntax, and it denotes an
  exact rational just as a plain decimal does, so it rounds by the same single rule. Hex float
  literals do not exist in Luau and are not added.
- A literal whose magnitude rounds above the range is a **compile error**
  (`number literal out of range`). A literal that is not zero but rounds to zero is a
  `moo-analyze` warning (`number literal below resolution`), not an error.
- `tonumber` and string-to-number coercion use the same parser and the same rounding, and
  return `nil` on out of range.
- The compiler stores number constants as the raw int64 in a moo bytecode version (built:
  `Bytecode.h`, "moo bytecode versions"); the VM refuses Luau bytecode by version, by name.

## Division by zero

`a / 0`, `a // 0` and `a % 0` raise the named error `division by zero`. (`fixDiv` itself would
saturate; moo checks first.) NaN and infinity do not exist, so `math.huge` is the largest
value and `math.isnan` always returns false. `math.isinf` returns false; `math.isfinite`
returns true.

## Exact integers

Every integer with magnitude below 2^47 is exact. "Integer-valued" means the 16 fraction bits
are zero.

- **Table array part:** an integer-valued key `k` with `1 <= k <= sizearray` uses the array
  part, exactly as today.
- **Non-integer keys use the hash part**, hashed by the raw int64, as Lua and Luau already do
  for `t[1.5]`. They are not refused, because refusing would make a key valid or an error
  depending on rounding in the arithmetic that produced it, and would break ordinary Luau
  code (tables keyed by computed values) for no determinism gain: hash placement is already
  deterministic.
- **`#`** returns an integer-valued number.
- **Numeric `for`** loops run on fixed values. The trip count is computed exactly in integer
  arithmetic from start, limit and step before the loop starts, so `for i = 0, 1, 0.1` runs a
  fixed, platform-independent number of times. A zero step is a named error.
- **Functions that need an integer** (`string.byte`, `string.char`, `string.sub`,
  `string.rep`, `table.insert`/`remove`/`move`/`create` positions, `table.unpack`/`unpack`
  bounds, `select` index, `math.ldexp` exponent, `bit32`, `buffer` offsets and integer values,
  `string.format("%d")` and `string.pack` integer formats — both in the C library and in every
  `VM/src/lbuiltins.cpp` fastcall) accept only integer-valued numbers. A non-integer argument is
  the named error `number has no integer representation`, as in Lua 5.3, instead of Luau's silent
  truncation.

## math library

Rebuilt on `fixed`. No libm.

| function | source |
|---|---|
| `sin`, `cos` | `fixSin`, `fixCos` (`fixComputeCosSin`) |
| `atan2` | `fixAtan2` |
| `sqrt` | `fixSqrt` (exact, rounded toward zero; negative is a named error) |
| `exp2`-based: `exp`, `sinh`, `cosh`, `tanh` | `fixExp2` with exact scaling by log2(e) |
| `log2`-based: `log`, `log10`, `log(x, base)` | `fixLog2`; `x <= 0` is a named error |
| `tan` | `sin / cos`; a zero cosine is `division by zero` |
| `asin`, `acos`, `atan` | `fixAtan2` with `fixSqrt`; out-of-domain input is a named error |
| `floor`, `ceil`, `abs`, `min`, `max`, `clamp`, `sign` | exact integer operations |
| `round` | exact, half away from zero (Luau's rule) |
| `fmod`, `modf`, `frexp`, `ldexp` | exact integer operations on the raw value; `ldexp` saturates |
| `lerp` | Luau's formula `t == 1 ? b : a + (b - a) * t` in fixed |
| `noise` | Luau's Perlin noise, ported to fixed |
| `random` | PCG32 output mapped to fixed (see DETERMINISM.md, 10) |
| `pi`, `huge` | nearest Q48.16 to pi; the largest value |

**Accuracy oracle:** [CORE-MATH](https://core-math.gitlabpages.inria.fr/) gives the correctly
rounded double result of each real function; converting that exactly to Q48.16 gives the ideal
answer. Layer 4 measures every function's maximum error in raw units (2^-16) against it,
exhaustively where the domain allows and by dense sampling elsewhere, and publishes the table.
Known today from `fixed`'s own documentation: `sqrt` is exact (error below one raw unit,
toward zero); `atan2` is within about 0.0023 degrees (about 2.6 raw units). The other bounds
are not yet measured and are not claimed.

`fixSqrt` seeds its integer square root with the IEEE double square root and then repairs the
result to the exact integer answer. IEEE `sqrt` is correctly rounded on every supported
platform, so this is deterministic, and the double never leaves the function; but it is a
double inside the VM. Layer 4 either keeps it with that argument written down, or switches to
`fixed`'s integer-only path (`fixISqrt128High`); Glenn decides.

**`pow` and `^`:**

- **Integer-valued exponent:** exact binary exponentiation (repeated squaring) in a wide
  intermediate, rounded **once** at the end, ties to even, saturating. `0 ^ negative` is
  `division by zero`. The target bound is under one raw unit for every result in range; layer
  4 proves it by exhaustive test on small bases and the CORE-MATH comparison elsewhere.
- **Non-integer exponent:** `fixPow`, which is `fixExp2(exponent * fixLog2(base))`. A negative
  base is a named error. Its error is relative and grows with the exponent: an absolute error e
  in `log2` becomes a relative error of about ln(2) · |exponent| · e in the result. Layer 4
  measures the bound and reports it before the math library is locked.

## Number formatting

Exact, deterministic, integer arithmetic, no libc `printf`:

- `tostring(x)` prints the **shortest decimal that reads back to exactly the same raw value**
  (`tostring(0.1)` is `0.1`, and `tonumber(tostring(x)) == x` bit for bit, always).
  Integer-valued numbers print with no fraction (`3`, not `3.0`), as Luau does.
- The full exact value is always available: every Q48.16 value has a finite decimal expansion
  of at most 16 fraction digits, which `string.format("%.16f", x)` prints.

Shortest round-trip is proposed over printing the full expansion by default because it keeps
Luau's behaviour (Luau's own printer is a shortest round-trip printer) and it is still exact
in the sense that matters: it identifies the value uniquely. Glenn may prefer the full
expansion; it is one switch.

## string.format

`%f`, `%e`, `%g` (and `%E`, `%G`) are **implemented deterministically**, not refused: the exact
decimal expansion of the value is rounded to the requested precision, ties to even on the exact
value, by moo's integer formatter. Width, flags and precision follow C's rules. `%d`, `%i`,
`%x`, `%X`, `%c` require an integer-valued number (see "Exact integers"). `%a` is not supported
by Luau's `string.format` and stays unsupported. `%q` of a number uses `tostring`.

## Boundary

- The C API's `lua_Number` becomes `fixed_t`. `lua_pushnumber`, `lua_tonumber`,
  `luaL_checknumber` and the rest keep their names and take or return `fixed_t`.
- Host floats never enter the VM. moo adds two boundary functions with one exact rule:
  - `moo_fromdouble(double, fixed_t*)`: decodes the IEEE bit pattern in integer arithmetic and
    rounds to the nearest Q48.16 value, **ties to even**. Negative zero becomes zero. NaN,
    infinity and out-of-range values return an error status; the host chooses what to do.
    `moo_fromfloat` is the same for float.
  - `moo_todouble(fixed_t)` and `moo_tofloat(int64_t)`: exact for magnitudes below 2^37; above
    that, round to nearest even, in integer arithmetic.
- The boundary rule is `moo_numtodoublebits` / `moo_numtofloatbits`; `writef32` / `writef64` and
  `string.pack` encode through it.
- This is deliberately not `fixed`'s `fixFromDouble`, which rounds half away from zero by
  adding 0.5 in floating point and is undefined out of range. moo proposes the exact
  conversion upstream to `fixed`, so there is one rule in one place.
- Schema float fields convert by the same functions, so a value read from a schema record and
  the same value passed through the C API give the same bits. `buffer.readf32`/`readf64`,
  `string.unpack("f"/"d")` decode by the same rule; `writef32`/`writef64` and `string.pack`
  encode to the nearest float bit pattern, ties to even.

## Native codegen

**Excluded from the build** (Glenn, 2026-10-17). Luau's code generator computes on doubles, and
moo's number is fixed point; code that computes wrong answers is worse than absent code. So:

- the CMake option `MOO_CODEGEN` is off by default, and turning it on is a configure error;
- `Luau.CodeGen` (and the Makefile's `libluaucodegen.a`) is one file,
  `CodeGen/src/moo_codegen_off.cpp`, which answers every entry point with "not supported"
  (`luau_codegen_supported` returns 0, `compile` returns `CodeGenNotInitialized`), so the CLI and
  the tests take the interpreter path they already have;
- CodeGen's sources stay in the tree, unbuilt, so upstream merges into them stay mechanical;
- CodeGen's tests (assembly builders, IR builder and lowering, register allocation, code
  allocators) are not built; the conformance tests that exercise codegen return early when it
  is not supported;
- `moo --codegen*` and `--jit-inliner` are refused by name.
- The JIT bytecode inliner is excluded the same way (CMake option `MOO_JIT_INLINER`, off; on is a configure error):
  it folds number constants at run time as doubles. `Luau.Inliner` (and the Makefile's `libluaujitinliner.a`) is
  `Inliner/src/moo_jitinliner_off.cpp`, whose `luau_enable_jit_inliner` does nothing; the conformance case
  `JitInliner` still runs its script, on the interpreter. Found while the number swap was being finished, from a
  `-Wformat` warning in its constant dump.

A later layer reinstates it, rebuilt on the fixed type (no FMA, integer lowering), with its tests.

## Analyzer

`number` keeps its name and the type checker needs no new type: it never depended on the
representation. What changes is outside the type lattice: the builtin definitions have dropped
`os`, `vector` and the integer library, and the VM opens neither, so the analyzer and the runtime
agree; `moo-analyze` gains the literal-below-resolution warning. Layer 3b adds `vec3`, `quat` and
`transform` as builtin extern types, defined the way Luau already defines `vector`, with their operators
as metamethods; `quat.slerp` is not declared while the VM refuses it.

## vec3, quat and transform (layer 3b)

Luau's `vector` is three float32 components packed into the `TValue`. moo removes it in layer 3
(the library, the type, the bytecode constant and the C API, each refused by name; built in
`BytecodeBuilder.cpp`/`lvmload.cpp`, the compiler, `luaL_openlibs` and `lua.h`/`lualib.h`). Layer
3b, after
`number` = fixed, brings in three fixed-point types with **full Q48.16 components**, the same as
`number` and fixed3d:

| type | layout (fixed's C) | payload |
|---|---|---|
| `vec3` | `fixVec3 { x, y, z }` | 24 bytes |
| `quat` | `fixQuat { v: fixVec3, s }` | 32 bytes |
| `transform` | `fixTransform { p: fixVec3, q: fixQuat }`, which fixed3d's `b3Transform` is | 56 bytes |

None of these fits the 16-byte `TValue`. mat3 waits until a script needs inertia or skew; mat4
stays out because the renderer owns projection.

### Operations

All are values: immutable, compared by value, never aliased. Each operation calls the `fixed` C
function fixed3d calls (fixed3d's `b3*` vector functions are thin wrappers over them in
`include/box3d/fixed_compat.h`), so the bits match by construction:

| moo | fixed |
|---|---|
| `vec3 + vec3`, `vec3 - vec3`, `-vec3` | `fixVecAdd`, `fixVecSub`, `fixVecNeg` |
| `vec3 * vec3` (component-wise) | `fixVecMul` |
| `vec3 * number`, `number * vec3` | `fixMulSV` |
| `vec3 == vec3` | component equality |
| `vec3.dot`, `vec3.cross` | `fixDot`, `fixCross` |
| `vec3.length`, `vec3.normalize` | `fixLength` (exact 128-bit sum of squares, integer square root), `fixNormalize` |
| `quat * quat` | `fixMulQuat` |
| `quat * vec3` (rotate) | `fixRotateVector` |
| `quat.conjugate` | `fixConjugate` |
| `quat.nlerp` | `fixNLerp` |
| `quat.slerp` | **does not exist in `fixed` yet** (see below) |
| `transform * transform` (compose) | `fixMulTransforms` |
| `transform.inverse` | `fixInvertTransform` |
| `transform * vec3`, `transform.point` | `fixTransformPoint` |
| `transform.direction` | `fixRotateVector(t.q, v)` |
| `transform.invpoint`, `transform.invdirection` | `fixInvTransformPoint`, `fixInvRotateVector(t.q, v)` |

`quat.slerp` is added **upstream to `fixed`** first (built from `fixAtan2`, `fixSqrt` and
`fixComputeCosSin`), so fixed3d and moo share one implementation. Until then moo exposes `nlerp`
only, and `quat.slerp` is the named error `quat.slerp waits for fixed's slerp`. `quat.nlerp` refuses
an alpha outside [0, 1]. Luau's `vector` library names (`create`, `dot`, `cross`, `magnitude`,
`normalize`) are kept as aliases on `vec3`, so Luau vector code carries over.

**Saturation, stated precisely.** moo's own operators on these types (`+`, `-`, `*`, unary `-`)
follow `number`'s rules: saturate, never wrap, named error in debug. Compound functions (cross,
rotate, compose, normalize) are `fixed`'s functions verbatim, and a checked build raises no named
error for them. At pin `4a70c64` (fixed #59-#62) every add, subtract and 128-bit reduction in
`fixed` saturates under `FIX_SATURATE` with the sign of the exact result: `fixVecAdd`/`fixVecSub`,
`fixDot`, and the intermediate sums inside `fixCross`, `fixRotateVector`, `fixMulQuat` and
`fixMulTransforms`. They were plain int64 adds at the earlier pin (signed overflow, undefined in
C). One gap remains: fixed3d's default build does not define `BOX3D_FIXED_SATURATE`, so `fixMul`
wraps there. Within range the results are identical. To make them identical everywhere,
moo proposes saturating adds in `fixed` under `FIX_SATURATE`, and the layer 3b oracle is fixed3d
built with `BOX3D_FIXED_SATURATE`. Overflow behaviour is tested separately. As built, fixed3d's
`BOX3D_FIXED_SATURATE` does not reach fixed's inline `fixMul` (it defines `FIX_SATURATE` after
including `fixed/fixed.h`), so the comparison also defines `FIX_SATURATE`; and `fixDot` wraps at
the top of the range, deterministically ("Layer 3b as built").

transform's compose, point and invpoint make their one position add with moo's saturating add
(`moo_numadd`/`moo_numsub`, in `moo_transform_mul`, `moo_transform_point` and
`moo_transform_invpoint`) instead of fixed's plain `fixVecAdd`/`fixVecSub`, so they equal
`fixMulTransforms`, `fixTransformPoint` and `fixInvTransformPoint` wherever those are defined, and
saturate where fixed's add would overflow; a checked build raises no named error for it. What this
buys is narrow: the add itself is defined everywhere, so a script can no longer reach behaviour
that differs between compilers through it. With a non-identity rotation, a position near the
range ends also reaches `fixRotateVector` or `fixInvRotateVector` (compose's input, invpoint's
subtract result), whose sums can still overflow, so outside fixed's defined range the result is
determined only for the identity rotation. `inverse` has no add (negation is exact on the
domain), so it is `fixInvertTransform` verbatim.

### Representations costed

Measured on Luau 0.741 itself, so the numbers are real VM costs, not estimates. Each variant is a
scratch build (nothing committed), interpreter only, `-O2`, on Apple M5 Pro, median of 20 runs per
program over Luau's own benchmark suite: 40 general programs and 3 vector programs.

- **(a)** is Luau's own `LUA_VECTOR_DOUBLE=1` build. It makes every vector a 24-byte immutable GC
  object, which is exactly the vec3 payload, with one allocation per vector operation.
- **(b)** is the `TValue` padded to 24, 32 and 40 bytes. 32 bytes holds a vec3 plus its tag;
  40 holds a quat.
- **The baseline** is stock 0.741: a 16-byte `TValue` with float32 vectors in the slot.

| | (a) GC object | (b) 24 B TValue | (b) 32 B TValue (vec3) | (b) 40 B TValue (quat) |
|---|---|---|---|---|
| general programs, time (geomean; min to max) | **0.997** (0.91 to 1.07) | 1.095 | 1.077 (0.89 to 1.42) | 1.181 (0.97 to 1.68) |
| general programs, peak RSS (geomean; max) | **0.999** (1.04) | 1.057 (1.40) | 1.115 (1.80) | 1.166 (2.19) |
| vector programs, time (geomean; min to max) | 2.58 (1.89 to 4.30) | 1.070 | 1.082 | 1.165 |
| one vector op (`acc + a*s - b + b*2`) | 5.3 ns | 1.5 ns | 1.9 ns | 1.9 ns |
| 1M numbers in an array | 15.7 MB | 23.5 MB | 31.3 MB | 39.1 MB |
| 1M number keys in a hash | 32.8 MB | 49.2 MB | 65.6 MB | 82.0 MB |
| 1M vec3 in an array | 46.9 MB | n/a | 31.3 MB | 39.1 MB |

The baseline is 1.9 ns per vector op, and 15.7 MB and 32.8 MB for the two number tables.
`TableCreate_number` and `LargeTableCtor_hash` take the worst wide-slot hits (1.42x and 1.29x at
32 bytes, 1.68x and 1.44x at 40). A transform needs a 64-byte `TValue`, which was not built; the
trend from 16 to 40 bytes says it would cost more again.

- **(a) Immutable GC objects.** No cost to code that does not use the types: the general suite
  is unchanged within noise, and numbers stay 16 bytes everywhere. Each operation that produces
  a value allocates: about 3.4 ns extra per op, a 1.9x to 4.3x slowdown on vector-bound programs,
  and 56 bytes per stored vec3 (16-byte slot plus a 40-byte object). Sizes are 40 bytes for
  vec3, 48 for quat and 72 for transform with moo's 16-byte GC header (the creation number,
  DETERMINISM.md 1). Luau's allocator already pops
  fixed-size classes off per-page free lists, so the measured 3.4 ns already includes a
  small-object free list; a dedicated per-type list would gain little. What remains is GC work,
  and the lever for it is the compiler. A value an arithmetic op produces into an
  expression temporary has never been stored, passed or captured, so the next op in the same
  expression may overwrite it in place. In `acc + a*s - b + b*2` that cuts four allocations to
  two. It is a compiler decision, so it is deterministic. The gain is an estimate until built.
- **(b) A wider TValue.** Fast vector ops (no allocation), but it taxes everything: +7.7% time and
  +11.5% memory on general code at 32 bytes (vec3 only), +18% and +17% at 40 bytes (quat), every
  number array and hash node twice the size, and the table and stack layouts that Luau's code
  assumes (`static_assert`s in `VM/src/lmem.cpp`, `kTValueSizeLog2` in CodeGen) all change. That
  is a large, permanent diff against upstream in exactly the files that merge most often.
  Transform still would not fit.
- **(c) A host-side handle arena.** Not built: it is dominated by (a). Handles are copied freely
  into tables, upvalues and stacks, so a slot can be reused only when no handle reaches it, which
  is a tracing GC over the arena. That is (a)'s work plus an indirection and a generation compare
  on every read. The generation check catches use-after-free, which tracing already rules out.
  Slot reuse order would also be one more piece of state that the determinism guarantee has to
  pin.

**Recommendation: (a) for all three types**, with the expression-temporary reuse in the compiler.
It is the only option with zero cost to code that does not use the types, the only one that holds
a transform, and the smallest upstream diff (one GC object kind per type, no change to `TValue`).
Its cost falls on vector-heavy script loops; in the intended use the heavy vector math (physics)
runs in fixed3d's C, not in scripts. If a vec3-bound script ever matters, (b) at 32 bytes for vec3 alone
is the fallback: about 8% everywhere, with quat and transform staying as objects.

### Conformance (layer 3)

Bit-exact, in a release build, against `fixed`'s own C at the pin, built with `FIX_SATURATE` as
moo builds it: `fixMul` and `fixDiv` for `*` and `/`, and for `+ - // %`, unary `-` and the
comparisons a second definition from the table in "Semantics", written in fixed's 128-bit
arithmetic (`tests/moo/moo_oracle_fixed.c`); the comparisons are the table's int64 compares.
Every operator runs four ways: on `moo_number.h`, in the VM on operands passed through the C API,
folded by the compiler from literals, and in the K-operand opcodes. All four must give the
oracle's bits, and `/ // %` by zero must raise `division by zero` on every path. The operands are
an edge set crossed with itself: zero, one raw unit, 0.5, 1, every fraction bit, the ties of
`fixMul`, 2^31, 2^32, 2^47 and 2^48 raw, the saturation edges of `*` and `//`, and the ends of the
range. To these are added PCG32 pairs with a fixed seed, 2^16 on every push and 2^24 in the
nightly. A planted one-unit error in `*`, and a one-unit error on any one of the VM, fold and K
paths, must turn the suite red, and a bytecode dump pins that the literal paths fold and reach
the K opcodes.

### Conformance (layer 3b)

Bit-exact against fixed3d's own vec, quat and transform functions over dense inputs: random
operands across the full range, unit and near-unit quaternions, axis-aligned and degenerate
vectors (zero, single component, the saturation edges), and every operation in the table above.
Each case runs in moo and in C against the same `fixed` pin, and the bits must match. Overflow
and saturation cases are compared against fixed3d built with `BOX3D_FIXED_SATURATE`. Built: the 3b
oracle in `Moo.Test` with its reversed witness, and the fixed3d comparison (`tools/moo-fixed3d-compare.sh`, in the
`full` CI job, 0 differences), with their results and two upstream findings, are in "Layer 3b as built".

## integer

Luau 0.741 has an experimental int64 `integer` type and library. moo has one number type, so
moo opens the libraries without it (Luau's own `lualibs_NOINTEGER`) and the analyzer has no
`integer`. The type tag stays in the C API, unused, so upstream merges in that area stay mechanical.
An integer literal (`1i`, `0x10i`) is the parse error `moo has no integer type`, and no
`AstExprConstantInteger` is built.

## The rest of determinism

Each is detailed, with file and line, in [DETERMINISM.md](DETERMINISM.md):

- **Object keys** hash by a per-VM creation sequence number stored in the object header, never
  by address. A vec3, a quat and a transform are values: each hashes by its raw components (three,
  four and seven). Strings hash with Luau's existing fixed function and no random seed. Rehash and
  resize order need no change.
- **Iteration:** `next`/`pairs` order is a documented function of insertion and resize history.
- **GC:** no clock anywhere. Pacing depends only on allocation units, computed by a
  platform-independent formula. Weak tables and C userdata destructors are kept; destructors
  are queued by the sweep and run at the end of the GC step in creation-sequence order. Luau has
  no script `__gc`, and moo does not add one.
- **Removed:** the `os` library, every clock and time read, and environment seeding of
  `math.random`. Luau has no `io`. The random seed is a host input.
- **Output:** `tostring` of a table, function, userdata, thread or buffer prints its creation
  number, never an address. A vec3, a quat and a transform print their components:
  `vec3(x, y, z)`, `quat(x, y, z, s)` and `transform(vec3(px, py, pz), quat(qx, qy, qz, qs))`.
- **`collectgarbage`** accepts only `"collect"` and `"count"` (in allocation units); `gcinfo`
  reports allocation units.
- **Errors and tracebacks** are deterministic strings. The layer 5 audit found no numeric
  conversion specifier in the VM's error or traceback formats (only integer line and frame
  counts), so no change was needed; `tests/determinism/errors.luau` pins every error string and
  traceback byte for byte in the determinism suite.
- **Coroutines** are cooperative and already deterministic. Layer 5 tests them adversarially:
  `tests/determinism/coroutines2.luau` in the determinism suite, the conformance script
  `tests/conformance/moo/l3/moo-l5-coroutine-programs.luau`, and the C++ cases in
  `tests/moo/moo-l5-coroutine-programs.test.cpp`. The TLA+ model of their resume/yield/close/error
  states is `tla/Coroutines.tla`.

## State machines and TLA+

Per house practice, each state machine moo adds or changes gets a TLA+ module beside it, checked
with TLC on a small instance before the code is merged: the GC pacing and destructor queue
(layer 2, `tla/DestructorQueue.tla`) and the numeric `for` trip count (layer 3,
`tla/NumericFor.tla`), and the coroutine resume/yield/close/error states (layer 5, `tla/Coroutines.tla`, with
two plants).

## Upstream merges

The fixed-point VM touches far more of the tree than the fork does, so the merge cost is
designed in, not hoped for:

- **Number operations stay behind Luau's existing seams.** Luau already funnels arithmetic
  through `lua_Number`, the `luai_num*` macros in `VM/src/lnumutils.h`, `setnvalue`/`nvalue`
  and the fastcall builtins. moo changes those definitions instead of every call site.
- **New code goes in new files** (`VM/src/moo_*.cpp`, `Compiler/src/MooFold.cpp`,
  `extern/fixed`). An upstream change to a file moo never edits merges with no conflict.
- **Moo patches are a short, named series**, each one layer's change, kept rebased so a merge
  conflict lands in one known patch.
- **A patch inventory** (`docs/PATCHES.md`, written in layer 3 against the final layer 3 tree)
  lists every upstream file moo modifies or deletes and why; moo-added files are listed by
  directory. It is the merge checklist, and it is checked: the push gate runs
  `tools/moo-check-patches.sh` (with `fetch-depth: 0`) and fails when an upstream file is changed
  without a row, or a row no longer differs.
- **Untouched areas stay upstream:** the analyzer, the AST, the CLI (beyond the rename), the
  require library and CodeGen (while it is off). Their upstream tests keep running unmodified.
- **One merge per upstream release**, as the README says: merge the tag, rebase the moo series,
  run the full conformance and determinism suites on every platform, and record the conflict
  count for that release so the merge cost is measured.

## Layers

Each layer is built only after the one below it is secured: modelled where it has states,
tested including every refusal, read by a second reader, used for real, and its lock agreed
with Glenn.

1. **Fork.** Luau 0.741 with full history, the archive hash checked, the minimal rename
   (`moo`, `moo-analyze`, `moo-compile`, `.moo`), `-ffp-contract=off`, CI on Luau's unmodified
   suites, and these two documents. *Built; this tree.*
2. **Determinism without numbers.** Creation-sequence hashing and `tostring`; GC pacing by
   allocation units with no clock; the destructor queue; `os` removed; the host-seeded
   `math.random`; `collectgarbage` restricted; native codegen off. Doubles are still the number
   type. Checked by: Luau's suite (adapted only where it asserted the removed behaviour) and the
   first determinism tests (object-keyed tables, heavy GC with destructors and weak tables)
   matching across platforms.
3. **`number` = fixed.** `fixed_t` in the `TValue`; operators with saturation and named errors;
   literals; the compiler's folding and the bytecode constant; integer-valued indexing, `#` and
   `for`; the C API's `lua_Number`; the boundary functions; `vector` and `integer` out. Checked
   by: bit-exact agreement with `fixed`'s C on every operator over random and edge operands (the
   `moo-oracle` suite, above "Conformance (layer 3)"). *Built, awaiting its lock.*
3b. **vec3, quat and transform.** Full Q48.16 components as immutable GC objects, the operations
   above on `fixed`'s functions, slerp upstreamed to `fixed`, and expression-temporary reuse in
   the compiler. Checked by: bit-exact agreement with fixed3d's vec, quat and transform over
   dense inputs, and the representation benchmarks rerun against the numbers above. *Built: the
   three objects, their operations, library, C API and analyzer types, the 3b oracle with its reversed
   witness and the fixed3d comparison, 0 differences ("Layer 3b as built"). Not built: expression-temporary reuse; `quat.slerp`
   waits for `fixed`; no layer 3b note records the representation benchmarks rerun. Awaiting its
   lock.*
4. **math and formatting.** The math library on `fixed`; `pow`/`^`; `tostring`, `tonumber`
   and `string.format`; float bytes in buffers and `string.pack`. Checked by: the CORE-MATH
   error table, round-trip of every formatted value, and bit-exact agreement with `fixed`.
5. **Conformance.** The Luau suite adapted for fixed; every moved or skipped upstream case states
   its reason in a `moo:` comment and, where it names the moo test that replaces it, that test
   resolves; `tools/moo-check-skips.sh` lists them and fails on a missing reason or a replacement
   that does not exist. A cross-platform determinism suite that runs each program on Linux gcc and
   clang, macOS arm64, and Windows with MSVC and clang-cl and compares full VM state hashes and
   effect logs, including adversarial programs (object-keyed tables, heavy GC with destructors and
   weak tables, coroutines); bit-exact agreement with `fixed`'s C; libFuzzer targets for the
   operators and the number formatter and parser (`fuzz/moo_arith.cpp`, `fuzz/number.cpp`,
   `fuzz/format.cpp`); and the coroutine TLA+ model (`tla/Coroutines.tla`, three cases in `tla/CASES.tsv`). The Windows pair runs in the nightly, which gates nothing: it builds and runs
   `Moo.Test` (the determinism suite and the math oracle) under both compilers and fails when
   either hash differs from `tests/determinism/expected.txt`.
6. **First adoption.** An existing Luau consumer switches from Luau to moo, and its own
   determinism checks pass.
