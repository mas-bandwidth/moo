# moo-l4-math-exact-ops

`math.fmod`, `math.modf`, `math.frexp`, `math.ldexp`, `math.deg`, `math.rad`, `math.lerp`, `math.map`,
`math.isnan`, `math.isinf`, `math.isfinite`, and the `math` constants (`pi`, `huge`, `nan`, `e`, `phi`,
`sqrt2`, `tau`) move from the double bridge to exact integer operations on the raw Q48.16 value. Both
`VM/src/lmathlib.cpp` and the bytecode fastcalls in `VM/src/lbuiltins.cpp` call shared helpers in
`VM/src/lmathlib.cpp` so the library and the fastcall always produce identical bits.

## Decisions

1. **`fmod(a, b)` (`moo_math_fmod`):**
   Both operands share the `2^-16` scale, so the truncated remainder is the integer remainder `a % b`
   (with `b == -1` guarded to `0` so no signed-division overflow can ever occur). When `b == 0`,
   `moo_math_fmod` returns `false`: `math_fmod` raises the named error `division by zero`
   (`docs/DESIGN.md`, "Division by zero"), and `luauF_fmod` returns `-1` so the VM falls back to
   `math_fmod` and raises the same error.
2. **`modf(a)` (`moo_math_modf`):**
   Splits the magnitude `u = |a|` into its low 16 fraction bits `f = u & 0xffff` and integer bits
   `i = u - f`, restoring the sign of `a` to both (`0` when a part is zero). `ip + fp == a` holds
   bit-for-bit across the full 63-bit Q48.16 range, including `|a| >= 2^37` where a 53-bit `double`
   bridge previously lost the fraction bits.
3. **`frexp(a)` and `ldexp(a, e)` (`moo_math_frexp`, `moo_math_ldexp`):**
   - For `a == 0`, `frexp` returns `(0, 0)`. For `a != 0` with magnitude `u = |a|` and highest set bit
     index `msb` in `0..62` (`2^msb <= u < 2^(msb + 1)`), `|a| = u * 2^-16 = (u * 2^-(msb + 1)) * 2^(msb - 15)`,
     so the exponent is `e = msb - 15` (in `[-15, 47]`) and the binary mantissa magnitude in `[0.5, 1)`
     (raw `[32768, 65535]`) is `u << (15 - msb)` when `msb <= 15` and `u >> (msb - 15)` when `msb > 15`.
     Shifting toward zero for `msb > 15` keeps the top 16 significant bits of `u` and guarantees that
     the mantissa stays strictly in `[0.5, 1)` (`raw <= 65535 < 65536 = 1.0`), whereas rounding could
     carry `2 - 2^-16` (`raw = 131071`) up to `1.0` (`65536`).
   - `ldexp(a, e)` shifts `u = |a|` left by `e` when `e > 0`, saturating to `MOO_NUMBER_MAX`
     (`INT64_MAX`) or `MOO_NUMBER_MIN` (`-INT64_MAX`) when `e >= 63` or `u > (uint64_t(MOO_NUMBER_MAX) >> e)`,
     and shifts `u` right by `-e` toward zero when `e < 0` (`0` for `e <= -63`). A non-integer exponent
     is refused by `luauF_ldexp` (`!luai_numisint`, returning `-1`) and raises
     `number has no integer representation` in `math_ldexp` via `luaL_checkinteger`.
4. **`deg(a)` and `rad(a)` (`moo_math_deg`, `moo_math_rad`):**
   Each is one `fixMul` (round half up, saturating) by the nearest Q48.16 constant:
   - `180 / pi = 57.295779513082320876798...`: scaled by `2^16 = 65536`,
     `(180 / pi) * 65536 = 3754936.20612936296...`, which rounds to nearest (both ties-to-even and
     half-up, since the fraction is `0.2061... < 0.5`) as `MOO_MATH_DEG_PER_RAD = 3754936`
     (`57 + 19384 / 65536 = 57.2957763671875`).
   - `pi / 180 = 0.017453292519943295769...`: scaled by `2^16 = 65536`,
     `(pi / 180) * 65536 = 1143.81897858822028...`, which rounds to nearest (both ties-to-even and
     half-up, since the fraction is `0.8189... > 0.5`) as `MOO_MATH_RAD_PER_DEG = 1144`
     (`1144 / 65536 = 0.0174560546875`).
5. **`lerp(a, b, t)` (`moo_math_lerp`):**
   Evaluates Luau's formula `t == 1 ? b : a + (b - a) * t` directly on moo's fixed operators:
   `t == MOO_NUMBER_ONE ? b : moo_numadd(a, moo_nummul(moo_numsub(b, a), t))`.
6. **`math.pi`, `math.huge` and the remaining constants / predicates:**
   - `math.pi` is `MOO_MATH_PI = 205887` (`pi * 65536 = 205887.4161458...`, nearest Q48.16 to `pi`,
     matching `FIX_PI` in `extern/fixed`).
   - `math.huge` is `MOO_NUMBER_MAX` (`INT64_MAX = 9223372036854775807`).
   - `math.e = 178145`, `math.phi = 106039`, `math.sqrt2 = 92682`, `math.tau = 411775`, `math.nan = 0`
     are defined as exact `fixed_t` constants rather than calling `moo_numbridgefromdouble` at startup.
   - `math.isnan` and `math.isinf` always return `false`, and `math.isfinite` always returns `true`
     (`docs/DESIGN.md`, "Division by zero"), on both the library and fastcall paths.
   - `math.map(x, inmin, inmax, outmin, outmax)` evaluates
     `outmin + (x - inmin) * (outmax - outmin) / (inmax - inmin)` on `moo_numadd`, `moo_numsub`,
     `moo_nummul` and `moo_numdiv`, raising `division by zero` when `inmax == inmin`.

## What changed

- `VM/src/lmathlib.cpp`:
  - Replaced `LUAU_PI`, `RADIANS_PER_DEGREE`, `LUAU_NAN`, `LUAU_E`, `LUAU_PHI`, `LUAU_SQRT2`, `LUAU_TAU`
    with exact `fixed_t` constants (`MOO_MATH_PI`, `MOO_MATH_DEG_PER_RAD`, `MOO_MATH_RAD_PER_DEG`,
    `MOO_MATH_E`, `MOO_MATH_PHI`, `MOO_MATH_SQRT2`, `MOO_MATH_TAU`).
  - Added shared helpers `moo_math_fmod`, `moo_math_modf`, `moo_math_frexp`, `moo_math_ldexp`,
    `moo_math_deg`, `moo_math_rad`, and `moo_math_lerp`.
  - Updated `math_fmod`, `math_modf`, `math_deg`, `math_rad`, `math_frexp`, `math_ldexp`, `math_map`,
    `math_lerp`, `math_isnan`, `math_isinf`, `math_isfinite`, and `luaopen_math` to use exact fixed
    operations with no `moo_numbridge*` calls.
- `VM/src/lbuiltins.cpp`:
  - Added forward declarations for `moo_math_fmod`, `moo_math_modf`, `moo_math_frexp`, `moo_math_ldexp`,
    `moo_math_deg`, `moo_math_rad`, and `moo_math_lerp`.
  - Updated `luauF_deg`, `luauF_fmod`, `luauF_frexp`, `luauF_ldexp`, `luauF_modf`, `luauF_rad`,
    `luauF_lerp`, `luauF_isnan`, `luauF_isinf`, and `luauF_isfinite` to use the shared fixed helpers.
- `tests/Conformance.test.cpp`:
  - In `runConformance`, for `moo/vector_library.luau` (until `moo-l3-libs-out` removes the float32
    vector library), widened `fuzzyeq`'s default tolerance from `1e-6` (which rounds to `0` raw in
    Q48.16) to `1e-3` (`66` raw) so `math.rad(ans[i])` via `fixMul(a, 1144)` (`MOO_MATH_RAD_PER_DEG`)
    agrees within tolerance with float32 `vector.angle` across the double bridge.
- `tests/moo/moo-l4-math-exact-ops.test.cpp` (new):
  - `MathExactOpsConstants`, `MathExactOpsFmod`, `MathExactOpsModf`, `MathExactOpsFrexp`,
    `MathExactOpsLdexp`, `MathExactOpsDegRad`, `MathExactOpsLerp`, and `MathExactOpsConformanceScript`,
    testing edge cases and 4096 seeded inputs on both the library (`-O0`) and fastcall (`-O2`) paths
    against an independent 128-bit integer reference in the test.
- `tests/conformance/moo/l3/moo-l4-math-exact-ops.luau` (new):
  - VM conformance script run by `MooL3Scripts` checking `pi`, `huge`, `fmod`, `modf`, `frexp`, `ldexp`,
    `deg`, `rad`, and `lerp` on both fastcall and library paths.

## DESIGN.md sentences that must change

- `docs/DESIGN.md`, "Layer 3 as built", "TEMPORARY bridges":
  - `fmod`, `modf`, `frexp`, `ldexp`, `deg`, `rad`, `lerp`, `map`, `isnan`, `isinf`, `isfinite` and the
    `math` constants (`pi`, `huge`, `e`, `phi`, `sqrt2`, `tau`, `nan`) no longer cross the double bridge.
- `docs/DESIGN.md`, "math library" table:
  - Add row for `deg`, `rad`:
    > | `deg`, `rad` | `fixMul` by nearest Q48.16 of `180/pi` (`3754936` raw) and `pi/180` (`1144` raw) |
  - Update `fmod`, `modf`, `frexp`, `ldexp` row to note that `b == 0` in `fmod` raises `division by zero`
    and `frexp` shifts the raw magnitude into `[0.5, 1)` toward zero.

## DETERMINISM.md sentences that must change

- `docs/DETERMINISM.md`, section 7 ("Platform libm in math, `^` and compile-time folding"):
  - Remove `fmod`, `modf`, `frexp`, `ldexp`, `deg`, `rad` and `lerp` from the list of functions in
    `VM/src/lmathlib.cpp` and `VM/src/lbuiltins.cpp` that still cross the double bridge.
