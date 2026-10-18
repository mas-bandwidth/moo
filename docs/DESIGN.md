# moo design

moo is Luau with two changes: everything in the language is deterministic, and `number` is
Q48.16 fixed point. Floats and doubles are outlawed inside the VM. Everything else (syntax,
tools, the C API) stays as close to Luau as possible.

**Status.** Layers 1-5 are built. The conformance record is the program-by-program matrix in
DETERMINISM.md, "Status". The section "Layer 3 as built" below is the state of `main`.
Everything else here is design.

The determinism guarantee, and every source of non-determinism found in Luau 0.741 with its
file and line, is in [DETERMINISM.md](DETERMINISM.md). This document says what replaces them.

## Layer 3 as built

- **The number is fixed.** `lua_Number` is `int64_t`, the raw Q48.16 value; `TValue` stores it where Luau stored the
  double. Every operator goes through `Common/include/moo_number.h` (add, sub, negate exact and saturating; `fixMul`;
  `fixDiv`; exact floor division and remainder), behind Luau's `luai_num*` seams. `/`, `//` and `%` by zero raise
  `division by zero`. The domain is [-INT64_MAX, INT64_MAX] raw.
- **The C API boundary.** `lua_pushnumber`, `lua_tonumber`, `luaL_checknumber` and the rest take and return the raw value.
  In C++ a `double`, `float` or `int` passed to `lua_pushnumber` is a compile error; hosts convert with
  `moo_fromdouble` / `moo_todouble` (moo.h). Building with `-DMOO_NUMBER_PROBE` makes `lua_Number` a struct, so every
  implicit use of a number is a compile error: that is how each site was found.
- **The compiler** folds number arithmetic with the same `moo_number.h` operations (a constant still travels as the
  double that holds it exactly); it does not fold division by zero, `^`, or any builtin call or `math` constant.
- **Native code generation** is excluded (below).
- **The suite says what it skips.** Every skipped doctest case and every moved conformance
  script states its reason with `moo:`; `tools/moo-check-skips.sh` lists them and fails when a
  reason is missing or names a replacement that does not exist.
- **The determinism suite runs on Windows.** The nightly builds `Moo.Test` (the determinism
  suite and the math oracle) on `windows-latest` with MSVC and with clang-cl and runs it; the
  job fails when either the effects hash or the state hash differs from
  `tests/determinism/expected.txt`. No frozen hash changes.
- **TEMPORARY bridges**, each marked in the code and each removed by a later layer 3 PR:
  - bytecode carries a double per number constant, rounded by the boundary rule at load, saturating (the bytecode
    version PR);
  - the libm functions of `math`, `^`, `math.noise`, and the float `vector` type convert through doubles
    (`moo_numbridgetodouble`, `moo_numbridgefromdouble`), until the math library is rebuilt on fixed and the vector
    type is removed;
  - `buffer.readf32`/`writef32`, `readf64`/`writef64` and `string.pack` floats round by the bridge (the boundary PR);
- **Literals are exact** (`Common/include/moo_parse.h`): the parser reads a decimal literal as the exact rational it
  denotes and rounds once to Q48.16, ties to even, with no libc and no locale; exponent notation stays; `0x` and `0b`
  literals are integers. A literal beyond the range is the compile error `Number literal out of range`; a nonzero
  literal that rounds to 0 is the lint warning "below the resolution". Until the bytecode version PR, the AST and
  the bytecode carry the double that holds the value, which is exact below 2^37.
- **Formatting and tonumber are exact** (`Common/include/moo_format.h`): `tostring` prints the shortest decimal that
  reads back to exactly the same value (`0.1 + 0.2` prints `0.3`, because both read as 19661/65536), integers with no
  fraction; `string.format`'s `%f`, `%e`, `%g`, `%E` and `%G` follow C's flags, width and precision on the exact decimal
  expansion, rounding half to even, with no libc. `tonumber` and string coercion use the literal parser, with spaces
  and one sign around it; out of range is not a number. A libFuzzer target feeds arbitrary strings
  to the parser and arbitrary raw values through the formatter and requires `parse(format(x)) == x`
  (`fuzz/moo_arith.cpp`, `fuzz/number.cpp`, `fuzz/format.cpp`).
- **Numeric `for` is exact** (`tla/NumericFor.tla`): `LOP_FORNPREP` computes the trip count once, in integer
  arithmetic on the raw values (`moo_numfortrips`), and keeps the iterations left in the limit's register; the index
  never steps past the limit and never saturates. `for i = 0, 1, 0.1` runs 10 times (0.1 is 6554/65536). A zero step
  is the named error `'for' step is zero`. The compiler's loop unrolling already counts small integer loops exactly.
- **Exact already:** `math.abs`, `floor`, `ceil`, `min`, `max`, `clamp`, `sign`, `round` and `random` (16 bits per draw),
  table keys (integer-valued numbers use the array part; numbers hash by their raw value), and every integer
  conversion (toward zero, saturating at the int range, so no undefined float-to-int cast). The SSE4.1 fastcalls of
  `floor`, `ceil` and `round`, chosen by CPUID, are gone: one code path everywhere.

## Representation

There is one number type. Its value is the raw signed 64-bit Q48.16 integer, `fixed_t`
(48 integer bits, 16 fraction bits), stored in the `TValue` where Luau stores the double.
`TValue` stays 16 bytes and the `LUA_TNUMBER` tag is unchanged.

- Resolution: 2^-16 (about 0.0000153).
- Range: -2^47 to 2^47 - 2^-16 (about ±1.4 × 10^14).
- There is no negative zero, no NaN and no infinity.

## Semantics

Arithmetic is the C library [`fixed`](https://github.com/mas-bandwidth/fixed), pinned at
commit `2f1ed913374ab047c061c66f5be737715f7d87b5`, the commit fixed3d vendors. moo vendors it
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

Debug builds (`LUAU_ENABLE_ASSERT` or a moo debug option) raise a named error instead:
`number overflow in '*'` (and so on per operator). Saturation is the release behaviour, so the
same program never gives different *values* in debug and release; it either gives the same
value or stops with the named error in debug.

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
- The compiler stores number constants as the raw int64 in a moo bytecode version; the VM
  refuses Luau bytecode by version, by name.

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
  `string.rep`, `table.insert` positions, `bit32`, `buffer` offsets, `string.format("%d")`)
  accept only integer-valued numbers. A non-integer argument is the named error
  `number has no integer representation`, as in Lua 5.3, instead of Luau's silent truncation.

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
  - `moo_todouble(fixed_t)`: exact for magnitudes below 2^37; above that, round to nearest even.
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
representation. What changes is outside the type lattice: the builtin definitions drop `os`,
`vector` and the integer library while those are out, and `moo-analyze` gains the
literal-below-resolution warning. Layer 3b adds `vec3`, `quat` and `transform` as builtin types,
defined the way Luau already defines `vector`.

## vec3, quat and transform (layer 3b)

Luau's `vector` is three float32 components packed into the `TValue`. moo removes it in layer 3
(the library, the type and the bytecode constant, each refused by name). Layer 3b, after
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
only. Luau's `vector` library names (`create`, `dot`, `cross`, `magnitude`, `normalize`) are kept
as aliases on `vec3`, so Luau vector code carries over.

**Saturation, stated precisely.** moo's own operators on these types (`+`, `-`, `*`, unary `-`)
follow `number`'s rules: saturate, never wrap, named error in debug. Compound functions (cross,
rotate, compose, normalize) are `fixed`'s functions verbatim. Two gaps exist at this `fixed` pin:
`fixVecAdd`/`fixVecSub` and the intermediate sums inside `fixCross`, `fixRotateVector` and
`fixMulTransforms` are plain int64 adds (signed overflow, undefined in C), and fixed3d's default
build does not define `BOX3D_FIXED_SATURATE`, so `fixMul` wraps there. Within range the results
are identical. To make them identical everywhere, moo proposes saturating adds in `fixed` under
`FIX_SATURATE`, and the layer 3b oracle is fixed3d built with `BOX3D_FIXED_SATURATE`. Overflow
behaviour is tested separately.

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
  and 48 bytes per stored vec3 (16-byte slot plus a 32-byte object). Sizes are 32 bytes for
  vec3, 40 for quat and 64 for transform with the GC header. Luau's allocator already pops
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

### Conformance (layer 3b)

Bit-exact against fixed3d's own vec, quat and transform functions over dense inputs: random
operands across the full range, unit and near-unit quaternions, axis-aligned and degenerate
vectors (zero, single component, the saturation edges), and every operation in the table above.
Each case runs in moo and in C against the same `fixed` pin, and the bits must match. Overflow
and saturation cases are compared against fixed3d built with `BOX3D_FIXED_SATURATE`.

## integer

Luau 0.741 has an experimental int64 `integer` type and library. moo has one number type, so
moo opens the libraries without it (Luau's own `lualibs_NOINTEGER`) and the analyzer has no
`integer`. The type tag stays in the C API, unused, so upstream merges in that area stay mechanical.

## The rest of determinism

Each is detailed, with file and line, in [DETERMINISM.md](DETERMINISM.md):

- **Object keys** hash by a per-VM creation sequence number stored in the object header, never
  by address. Strings hash with Luau's existing fixed function and no random seed. Rehash and
  resize order need no change.
- **Iteration:** `next`/`pairs` order is a documented function of insertion and resize history.
- **GC:** no clock anywhere. Pacing depends only on allocation units, computed by a
  platform-independent formula. Weak tables and C userdata destructors are kept; destructors
  are queued by the sweep and run at the end of the GC step in creation-sequence order. Luau has
  no script `__gc`, and moo does not add one.
- **Removed:** the `os` library, every clock and time read, and environment seeding of
  `math.random`. Luau has no `io`. The random seed is a host input.
- **Output:** `tostring` of a table, function, userdata, thread or buffer prints its creation
  number, never an address.
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
  states is owed.

## State machines and TLA+

Per house practice, each state machine moo adds or changes gets a TLA+ module beside it, checked
with TLC on a small instance before the code is merged: the GC pacing and destructor queue
(layer 2, `tla/DestructorQueue.tla`), the coroutine resume/yield/close/error states (layer 5), and the numeric `for`
trip count (layer 3, `tla/NumericFor.tla`). Each code change cites its model.

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
- **A patch inventory** (`docs/PATCHES.md`, written in layer 2) lists every upstream file moo
  modifies and why. It is the merge checklist, and it is checked: CI fails if a commit touches
  an upstream file that is not in it.
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
   by: bit-exact agreement with `fixed`'s C on every operator over random and edge operands.
3b. **vec3, quat and transform.** Full Q48.16 components as immutable GC objects, the operations
   above on `fixed`'s functions, slerp upstreamed to `fixed`, and expression-temporary reuse in
   the compiler. Checked by: bit-exact agreement with fixed3d's vec, quat and transform over
   dense inputs, and the representation benchmarks rerun against the numbers above.
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
   `fuzz/format.cpp`); and the coroutine TLA+ model. The Windows pair runs in the nightly, which
   gates nothing: it builds and runs `Moo.Test` (the determinism suite and the math oracle) under
   both compilers and fails when either hash differs from `tests/determinism/expected.txt`.
6. **First adoption.** An existing Luau consumer switches from Luau to moo, and its own
   determinism checks pass.
