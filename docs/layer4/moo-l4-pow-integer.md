# moo-l4-pow-integer

Layer 4's integer-exponent `pow`: `moo_pow_int` in `Common/include/moo_pow.h`. Every pow path calls it:
`luai_numpow` and the `^` opcodes, `math.pow` and its fastcall, and the compiler's constant folding. It
raises a raw Q48.16 base to an `int64_t` exponent exactly and rounds once, half to even. It saturates, and
it reports `0 ^ negative`. It uses integer arithmetic only, with no float or double. This note covers the
whole split of moo-l4-pow-integer. The design card (moo-l4-pow-integer-design) built the function and its
proof. The three pro cards below wire it in, and none of them changes it.

## The function

```cpp
#include "moo_pow.h"

#define MOO_POW_OK 0
#define MOO_POW_SATURATED 1        // *out is MOO_NUMBER_MAX or MOO_NUMBER_MIN
#define MOO_POW_DIVISION_BY_ZERO 2 // 0 ^ negative; *out is 0

inline int moo_pow_int(fixed_t base, int64_t exponent, fixed_t* out);
```

- `base` is the raw Q48.16 value, any `int64_t`. `exponent` is the integer exponent, any `int64_t`, so a
  caller holding an integer-valued number passes `raw >> 16`, which is exact.
- `MOO_POW_OK`: `*out` is `round_half_even(x^n)` as a raw value, in `[-INT64_MAX, INT64_MAX]`.
- `MOO_POW_SATURATED`: the rounded value is beyond the domain. `*out` is the edge with the result's sign:
  `MOO_NUMBER_MIN` when the base is negative and `n` is odd, otherwise `MOO_NUMBER_MAX`. That is the release
  value. A checked build raises `number overflow in '^'` instead. The test is the same one the other
  operators use: an exact result at the edge is not saturation, so `MAX ^ 1` is `MAX` with `MOO_POW_OK`, just
  as `MAX * 1` is.
- `MOO_POW_DIVISION_BY_ZERO`: `0 ^ n` with `n < 0`. `*out` is 0, and every build raises
  `division by zero` (docs/DESIGN.md, "Division by zero"). It is the only error.
- `x ^ 0 = 1` for every `x`, `0 ^ 0` included, as in Lua. `0 ^ n = 0` for `n > 0`. A nonzero base never
  reports division by zero, however small the result is.
- `moo_pow_int_certain(base, exponent, out, certain)` is the same function. It also reports whether the
  interval path's two bounds agreed. Only the tests call it.

## Decision: the wide intermediate

A Q48.16 base `b = a / 2^16` raised to `n` is the rational `V = a^n / 2^(16(n-1))` raw for `n > 0`, or
`2^(16|n|+16) / a^|n|` for `n < 0`. The answer is `V` rounded half to even. There are two representations,
each exact in what it claims.

1. **Exact, for |n| <= 13 (`MOO_POW_EXACT_MAX`).** `D = a^|n|` is held as an exact 256-bit integer
   (`fixed_int256.h`). For `n > 0`, `V = D >> 16(n-1)`, and the shifted-out bits decide the rounding. For
   `n < 0`, `V = 2^(16|n|+16) / D` is one `fixUInt256DivMod`, and `r` against `D - r` decides the rounding.
   If `D` does not fit in 256 bits, then `V >= 2^64` (for `n > 0`, it saturates) or `V <= 2^-32` (for
   `n < 0`, it rounds to 0). So every `|n| <= 13` is exact. 13 is the largest `n` for which
   `256 - 16(n-1) >= 64`.
2. **Interval, for |n| >= 14.** `P = (a / 2^16)^|n|` lies between two wide values, `lo <= P <= hi`. Each
   wide value is a normalized 128-bit mantissa with a tracked binary exponent: `m * 2^e`, with `m` in
   `[2^127, 2^128)`. They are built by left-to-right binary exponentiation (square, then multiply by the
   base when the bit is set). Every 128 x 128 -> 256 product is cut to 128 bits, rounded down for `lo` and
   up for `hi`. While no product has dropped a bit (the sticky bit is clear), `lo == hi` is the exact value
   and the multiply is done once. Exact results and exact ties (`1.5 ^ 17 = 3^17 / 2`) are therefore
   seen exactly. For `n > 0`, `V = 2^16 P`. For `n < 0`, `V = 2^16 / P`. The bounds of `V` are the two
   reciprocals `2^255 / m`, rounded down from `hi` and up from `lo` (one `fixUInt256DivMod` each). This
   is the only place a negative exponent differs from a positive one, and it is why the single rounding
   survives: the reciprocal is taken of the bracketed exact power, never of a rounded base. Each bound
   of `V` is rounded once, half to even. When the two agree, that value is the correctly rounded answer.
3. **Early exits.** After each step, the loop checks whether a bound already shows that the result
   certainly saturates or certainly rounds to 0. For a base above 1 that means `lo >= 2^48`
   (`n > 0`, `V >= 2^64`) or `lo >= 2^18` (`n < 0`, `V <= 2^-2`). For a base below 1 it means
   `hi < 2^-18` or `hi < 2^-48`. The power only moves away from 1 as the prefix of the exponent grows, so
   the exit is sound. It also keeps the tracked exponent within a few hundred, so `int` never overflows, and
   it bounds the loop at 63 steps for any `int64_t` exponent.

**Why this and not exact big integers throughout.** The exact power has about `16|n|` bits. A result in
range with `|n|` up to 2,135,042 needs about 34 million bits, which means allocation inside the VM. The
interval keeps a fixed 2 x 128 bits on the stack. Exactness is not given up: the proof below shows the two
bounds always agree.

## Why one rounding is exact

Outside the exact range (`|n| >= 14`) the result can be in range and nonzero only for bases near 1:
`|log2(a / 2^16)| < 47 / |n|`. That set is finite, and `MooPowIntCertainEverywhere` visits all of it.
For each `|n|` from 14 upward and each of the four walks (`n > 0` and `n < 0`, bases above and below 1),
it steps the base away from 1 one raw unit at a time. It stops at the first base that saturates or rounds
to 0 with the two bounds agreeing. On every call it checks that the bounds round to the same value. The
walks stop at `|n|` = 2,135,042, the last exponent with a live base (`(1 + 2^-16)^n < 2^47`), plus at
least 64 quiet exponents for each of the 16 threads, about 1,024 exponents past the last live one.

Result, on hulk (x86_64), clang 18 and gcc 13 alike: **76,620,463 calls, 0 uncertain**.

The steps the proof relies on:

- **The bounds are sound.** Each product is rounded down for `lo` and up for `hi`, and the reciprocal
  rounds the other way, so `lo <= P <= hi` holds by construction. The exact reference tests below check
  the answers.
- **What the walk skips is decided.** Fix `n`. Then `lo` and `hi` are nondecreasing in the base, because
  each directed-rounded product is monotone and the operation sequence is the same. So once a base
  saturates with both bounds agreeing, every larger base does as well, by exiting no later or by rounding
  no lower. The same holds downward for bases below 1, and with the directions reversed for `n < 0`.
- **Exponents past the walks are decided.** This step is argued by hand; the walks cannot reach it. The
  interval's relative width is below `2^-56` for every
  `|n| < 2^63`. There are at most 126 roundings, each of relative size at most `2^-127`, and each is
  amplified at most `|n| / prefix < 2^63` times. At the last walked exponent, the first base each side of
  1 is already decided at its edge. For a larger `|n|`, the exact value moves further past that edge by
  at least a factor `1 + 2^-17`, and that factor is far wider than the interval. `MooPowIntCertainSampled`
  spot-checks the argument: bases 65534, 65535, 65537 and 65538, with exponents `+-(2^k - 1 .. 2^k + 1)`
  for `k` = 22..62 (past 2,135,042), are all decided at the edge.

The cheap version (`MooPowIntCertainSampled`, 0.3 s) runs in every `Moo.Test`. It takes one exponent in
4096 from 14 to past 2,135,042, every exponent 14..20, and the spot-check above. The full proof does
**not** run in the DONE-WHEN gate: it is `doctest::skip()` (25 s of CPU, 1.8 s on 16 threads). This card
ran it by hand on both compilers. A change to `moo_pow.h` must rerun it, and a nightly job should run it.
Run it with `build/Moo.Test -ts=moo-l4-pow-integer-design -tc=MooPowIntCertainEverywhere --no-skip`.

**So the bound is half a raw unit.** Every result in range is correctly rounded, ties to even. That is
tighter than DESIGN.md's target of "under one raw unit". No CORE-MATH comparison is needed for integer
exponents.

## Consequences callers should know

- `x ^ -1` rounds half to even, where `1 / x` (`fixDiv`) truncates. They differ when the remainder is
  over half: `6 ^ -1` is 10923 raw and `1 / 6` is 10922 raw.
- `x ^ -n` is not `(x ^ -1) ^ n`: `3 ^ -2` is 7282 raw, the rounding of `65536 / 9`.
- `x ^ 2` equals `x * x` except at a tie: `fixMul` rounds half up, and pow rounds half to even.

## Cost

These are ns per call on hulk (x86_64, 64 cores, load 25.8 when measured), with `-O2`, 4,000,000 calls
per row, and bases varying over 256 raw values so nothing folds. The libm column is today's bridge,
`moo_numbridgefromdouble(pow(...))`.

| x ^ n | clang 18 | gcc 13 | clang, emulated int128 | libm bridge (clang) |
|---|---|---|---|---|
| 1.0046..1.0085 ^ 2 | 21.9 | 24.8 | 29.1 | 30.1 |
| 1.0046..1.0085 ^ 10 | 43.3 | 59.1 | 90.4 | 33.4 |
| 1.0000..1.0039 ^ 100 | 103.1 | 132.5 | 251.8 | 41.6 |
| 1.00002..1.0039 ^ 2^40 (saturates) | 169.6 | 288.7 | 498.4 | 14.8 |
| 0.9961..0.99998 ^ 2^40 (rounds to 0) | 120.0 | 246.5 | 413.3 | 42.9 |
| 1.0046..1.0085 ^ -2 | 19.3 | 23.9 | 30.8 | 28.3 |
| 1.0046..1.0085 ^ -10 | 73.4 | 85.4 | 375.3 | 36.8 |
| 1.0000..1.0039 ^ -100 | 228.9 | 313.1 | 1891.1 | 44.4 |
| 1.00002..1.0039 ^ -2^40 (rounds to 0) | 142.6 | 255.3 | 449.7 | 39.9 |

Small exponents are exact and at libm's cost or below it. `|n| >= 14` pays for two 128-bit products per
step. A negative exponent also pays for two 256/128 divisions at the end, and those divisions are the
slow part on the emulated arm (MSVC without `__int128`). The benchmark was a scratch program, not
committed. It compiles `Common/include/moo_pow.h` with
`-ICommon/include -Iextern/fixed/include -DFIX_SATURATE -O2` (add `-DFIX_FORCE_EMULATED_INT128` for the
emulated arm). It times the loop `for i: moo_pow_int(baseLo + (i & 255), n, &out)` with
`std::chrono::steady_clock` and prints the table above.

## Bit-identical across compilers

- `MooPowIntFrozenHash` pins an FNV-1a hash of (status, result) over the exhaustive small bases and
  65536 seeded cases with `|n| < 2048`: `11023186236270382986`. `build/Moo.Test` (clang 18) and
  `build-gcc/Moo.Test` (gcc 13) both pass it.
- The scratch benchmark's sweep hash (the small bases plus `|n|` 14..2996 near 1) is
  `15232445935574217061` on clang native, gcc native and clang with emulated int128 alike.
- The full proof gives the same counts on both compilers.

## Saturation, overflow errors and division by zero

`moo_pow_int` returns the release value together with a status, so a caller needs only one call:

- release: use `*out` for `MOO_POW_OK` and `MOO_POW_SATURATED`;
- checked build (`#if MOO_OVERFLOW_ERRORS`, always defined as 0 or 1 by `moo_number.h`; moo-l3-overflow-errors-debug): raise `number overflow in '^'` on
  `MOO_POW_SATURATED`, at the operation's own error site;
- every build: raise `division by zero` on `MOO_POW_DIVISION_BY_ZERO`.

No `moo_numpowsaturates(a, b)` was added to `moo_number.h`. A saturation test that is separate from the
value would compute the power twice in a checked build, so the VM card branches on the status instead.
`moo_number.h` is unchanged: the function needs no shared helper beyond what `fixed` already has
(`fixUInt128MulU64`, `fixUInt256DivMod`, `fixCountLeadingZeros64`).

## What changed

- NEW `Common/include/moo_pow.h`. It is header-only, so no build registry changed.
  - `moo_pow_int`, `moo_pow_int_certain`, and the `MOO_POW_*` statuses.
  - `moo_pow_detail::exactMagnitude`, `exactPower`, `intervalMagnitude`, `mul`, `mul128`, `reciprocal`,
    `roundWide` and `bumpUp`.
- NEW `tests/moo/moo-l4-pow-integer-design.test.cpp` (suite `moo-l4-pow-integer-design` in `Moo.Test`).
  Every case is checked against `Ref`, an exact rational reference: unbounded integers in 32-bit limbs,
  written in the test and sharing no code with `moo_pow.h` or `fixed`. `Ref` proves `R` is
  `round_half_even(N / D)` by comparing `2N` with `(2R +- 1) D`, and proves saturation by
  `2N >= (2^64 - 1) D`.
  - `MooPowIntExhaustiveSmallBases`: every raw base with `|b| <= 2^12`, exponents -8..8 (139,281 cases).
  - `MooPowIntExhaustiveNearOne`: every raw base within 2^12 of +-1 for exponents -8..8, and within 2^10
    of +-1 for exponents +-14..40 (the interval path), both signs of base and exponent, and base
    `INT64_MIN` (`|b| = 2^63`, outside the domain) for exponents -16..16.
  - `MooPowIntIntegerBases`: integer bases -64..64, exponents -24..24, including the range edges
    (`2 ^ 46` is in range, `2 ^ 47` saturates).
  - `MooPowIntSeeded`: 2^16 seeded cases (splitmix64), exponents -64..64. A quarter have bases of any size;
    the rest are near 1.
  - `MooPowIntLargeExponents`: 256 seeded cases with `|n|` 14..1013 against `Ref`, and exponents `2^40`,
    `INT64_MAX` and `INT64_MIN` with known answers.
  - `MooPowIntReportsSaturationAndDivisionByZero`: `0 ^ negative`, `0 ^ 0`, `0 ^ positive`, the saturating
    edges with their signs, and the exact edges `MAX ^ 1` and `MIN ^ 1` that are not saturation.
  - `MooPowIntRoundsOnceTiesToEven`: the exact tie `1.5 ^ 17`, the tie to 0 `0.5 ^ 17`, and the
    reciprocals that round where `fixDiv` truncates.
  - `MooPowIntCertainSampled` (every run) and `MooPowIntCertainEverywhere` (skipped, the proof).
  - `MooPowIntFrozenHash`: the bits, pinned.
- NEW this note.
- No VM, library, compiler or `moo_number.h` change. Today `^` and `math.pow` still use the libm bridge
  (`VM/src/lnumutils.h:19-20`).
- No TLA+ model: `moo_pow_int` is a pure function with no states or transitions. The proof is by
  exhaustion over its finite live domain, as described above.

## The pro cards

Each card calls `moo_pow_int` unchanged and adds no other pow path. An exponent is integer-valued when
`moo_numisinteger(c)` holds, and then it is `c >> 16` (`MOO_NUMBER_FRACTION_BITS`). A fractional
exponent stays on today's path until moo-l4-pow-fractional (`fixPow`).

### moo-l4-pow-integer-vm (luai_numpow and the ^ VM path)

1. `VM/src/lnumutils.h`: include `moo_pow.h`. Make `luai_numpow(a, b)` call
   `moo_pow_int(a, b >> 16, &r)` when `moo_numisinteger(b)`. Keep the bridge only for a fractional `b`,
   with its TEMPORARY comment narrowed to say so.
2. `VM/src/lvmexecute.cpp` `LOP_POW` and `LOP_POWK`: in the fast path, call `moo_pow_int` once for an
   integer-valued exponent. On `MOO_POW_OK` set the value. On `MOO_POW_SATURATED`, set the value in
   release, or leave for `luaV_doarithimpl<TM_POW>` under `#if MOO_OVERFLOW_ERRORS` (never `#ifdef`: the macro is always defined). On
   `MOO_POW_DIVISION_BY_ZERO`, always leave for `luaV_doarithimpl<TM_POW>`. This is the same shape as the
   12 arms of moo-l3-overflow-errors-debug, but with one call instead of a separate saturation test.
3. `VM/src/lvmutils.cpp` `luaV_doarithimpl`: for `TM_POW` with an integer-valued exponent, raise
   `division by zero` on `MOO_POW_DIVISION_BY_ZERO`, beside the existing `/`, `//` and `%` check. Under
   `#if MOO_OVERFLOW_ERRORS`, raise `number overflow in '^'` on `MOO_POW_SATURATED`, in the same `#if` block as
   the other five operators. This covers string-coerced operands.
4. `VM/src/lnumutils.h` comment block on checked arithmetic, and `Common/include/moo_number.h:105-107`:
   replace "^ is not checked while it is the libm bridge ..." with "`^` with an integer exponent is checked
   through `moo_pow_int`'s status (`Common/include/moo_pow.h`)".
5. Tests in that card's own file: `2 ^ 47` raises `number overflow in '^'` in a checked build and saturates
   in release, at optimization levels 0..2. `0 ^ -1` raises `division by zero` in both builds. The VM gives
   the same bits as `moo_pow_int` over a seeded sweep. Replace `MooOverflowPowUncheckedBridge`
   (`tests/moo/moo-l3-overflow-errors-debug.test.cpp`): a card may edit another card's case only when that
   case pins behaviour the card removes, and it must say so in its report.

### moo-l4-pow-integer-math (math.pow and its fastcall)

1. `VM/src/lmathlib.cpp` `math_pow`: for an integer-valued second argument, call `moo_pow_int`. Raise
   `division by zero` on `MOO_POW_DIVISION_BY_ZERO`, raise `number overflow in '^'` on
   `MOO_POW_SATURATED` under `#if MOO_OVERFLOW_ERRORS`, and push `*out` otherwise. Keep the fractional
   exponent on the bridge.
2. `VM/src/lbuiltins.cpp` `luauF_pow`: the same rule. On `MOO_POW_DIVISION_BY_ZERO`, or
   `MOO_POW_SATURATED` in a checked build, return -1 so the call falls back to `math_pow`, which raises.
   The fastcall never raises by itself.
3. Tests: `math.pow(x, n)`, the fastcall and `x ^ n` agree bit for bit over a seeded sweep and at the edges.

### moo-l4-pow-integer-fold (ConstantFolding)

1. `Compiler/src/ConstantFolding.cpp` `mooFold`: add `case AstExprBinary::Pow:`. When `b` is
   integer-valued, call `moo_pow_int(a, b >> 16, &r)`. Fold with `mooResult` only on `MOO_POW_OK`, and on
   `MOO_POW_SATURATED` in release only. Never fold `MOO_POW_DIVISION_BY_ZERO`: the run time raises it. Under
   `#if MOO_OVERFLOW_ERRORS`, the `mooSaturates` guard gains `Pow` through the same status. A fractional
   exponent stays unfolded. Make sure the upstream `case AstExprBinary::Pow:` double `pow` (`:574`) is
   never reached for numbers.
2. `Compiler/src/BuiltinFolding.cpp` `LBF_MATH_POW`: the same rule through `moo_pow_int`, or no fold.
   Today it folds with the build machine's double `pow` (DETERMINISM.md 7).
3. Tests: `tests/MooCompiler.test.cpp:54-55` pins `10 ^ 2` as `POWK` (not folded). Once this lands, it
   is folded to `100`, so that case changes in this card, which must say so. A folded constant equals the
   run time's bits. `0 ^ -1` and, in a checked build, `2 ^ 47` are not folded.

## DESIGN.md and DETERMINISM.md sentences that must change

On this card's landing:

- DESIGN.md, "math library", "`pow` and `^`", the integer-valued bullet. Replace "The target bound is
  under one raw unit for every result in range; layer 4 proves it by exhaustive test on small bases and
  the CORE-MATH comparison elsewhere." with:
  "`moo_pow_int` (`Common/include/moo_pow.h`) does it. For `|n| <= 13` the power is an exact 256-bit
  integer. Above that it is bracketed between two 128-bit-mantissa values rounded down and up at every
  product, and a negative exponent takes the reciprocal of that bracket. Every result is correctly
  rounded (at most half a raw unit, ties to even). That is proved by visiting every input whose result
  is neither saturated nor 0 and finding the two bounds agree on all 76,620,463. `x ^ -1` therefore
  rounds where `1 / x` truncates."

On the pro cards' landings (each card's note repeats its own sentence):

- DESIGN.md, "Layer 3 as built", TEMPORARY bridges: "the libm functions of `math`, `^`, `math.noise`, ..."
  becomes "the libm functions of `math`, `^` and `math.pow` with a fractional exponent, `math.noise`,
  ..." (moo-l4-pow-integer-vm and -math).
- DESIGN.md, "Layer 3 as built", "The compiler": "it does not fold division by zero, `^`, or any builtin
  call or `math` constant" becomes "it does not fold division by zero, `0 ^ negative`, `^` with a
  fractional exponent, or any other builtin call or `math` constant; `^` and `math.pow` with an integer
  exponent fold through `moo_pow_int`" (moo-l4-pow-integer-fold).
- DESIGN.md, "Overflow", as moo-l3-overflow-errors-debug's note rewrites it: "(`^` joins them when it is
  rebuilt on fixed)" becomes "and `^` with an integer exponent, by `moo_pow_int`'s status"
  (moo-l4-pow-integer-vm).
- DESIGN.md, "Division by zero": "`a / 0`, `a // 0` and `a % 0` raise the named error `division by zero`"
  becomes "`a / 0`, `a // 0`, `a % 0` and `0 ^ n` for an integer `n < 0` raise the named error
  `division by zero`" (moo-l4-pow-integer-vm).
- DETERMINISM.md, the status table row 7: "the libm functions and `^` still cross a double bridge" becomes
  "`^` and `math.pow` with an integer exponent are integer arithmetic (`moo_pow_int`); the other libm
  functions and a fractional exponent still cross a double bridge" (after -vm and -math). Section 7's
  bullet "`VM/src/lnumutils.h:12` `luai_numpow` is `pow` ..." gains "for a fractional exponent only".

## Not done

- No caller is wired. The three pro cards do that.
- The CORE-MATH comparison is not run for integer exponents. The exhaustive proof makes it unnecessary.
  It stays owed for a fractional exponent (moo-l4-pow-fractional).
- Cost was measured on x86_64 only (hulk). There is no ARM number.
