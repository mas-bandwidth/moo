# moo design

moo is Luau with two changes: everything in the language is deterministic, and `number` is
Q48.16 fixed point. Floats and doubles are outlawed inside the VM. Everything else (syntax,
tools, the C API) stays as close to Luau as possible.

**Status: design only.** Nothing in this document is built. The tree is layer 1 (the fork).
The layer list at the end goes to Glenn for correction before anything beyond the fork is
built. Where this document makes a choice ("moo does X"), it is a proposal until that review.

The determinism guarantee, and every source of non-determinism found in Luau 0.741 with its
file and line, is in [DETERMINISM.md](DETERMINISM.md). This document says what replaces them.

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

Off. moo runs the interpreter only (`luau_codegen_supported` returns false, and the CMake build
does not link CodeGen into the VM by default) until the code generator's IR and lowering are
rebuilt on `fixed_t`. The JIT inliner is off with it. CodeGen's own unit tests keep running
against the unmodified upstream code until then, so upstream merges into CodeGen stay checked.

## Analyzer

`number` keeps its name and the type checker needs no new type: it never depended on the
representation. What changes is outside the type lattice: the builtin definitions drop `os`,
`vector` and the integer library while those are out, and `moo-analyze` gains the
literal-below-resolution warning.

## vector

Luau's `vector` is three float32 components packed into the `TValue`. Three Q48.16 values do not
fit in 16 bytes. moo removes `vector` in layer 3 (the library, the type and the bytecode
constant, each refused by name). It comes back only as a fixed-point type, and whether that is
a boxed value or a wider `TValue` is a later decision for Glenn.

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
- **Errors and tracebacks** are deterministic strings: numbers in them go through moo's
  formatter.
- **Coroutines** are cooperative and already deterministic; layer 5 models them in TLA+ and
  tests them adversarially.

## State machines and TLA+

Per house practice, each state machine moo adds or changes gets a TLA+ module beside it, checked
with TLC on a small instance before the code is merged: the GC pacing and destructor queue
(layer 2), the coroutine resume/yield/close/error states (layer 5), and the numeric `for` trip
count (layer 3). Each code change cites its model.

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
4. **math and formatting.** The math library on `fixed`; `pow`/`^`; `tostring`, `tonumber`
   and `string.format`; float bytes in buffers and `string.pack`. Checked by: the CORE-MATH
   error table, round-trip of every formatted value, and bit-exact agreement with `fixed`.
5. **Conformance.** The Luau suite adapted for fixed; a cross-platform determinism suite that
   runs each program on Linux gcc and clang, macOS arm64 and Windows and compares full VM state
   hashes and effect logs, including adversarial programs (object-keyed tables, heavy GC with
   destructors and weak tables, coroutines); bit-exact agreement with `fixed`'s C; fuzzing; and
   the coroutine TLA+ model.
6. **First adoption.** An existing Luau consumer switches from Luau to moo, and its own
   determinism checks pass.
