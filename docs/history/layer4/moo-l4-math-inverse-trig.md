# moo-l4-math-inverse-trig

`math.atan2`, `math.atan`, `math.asin` and `math.acos` (and their bytecode fastcalls) move from the libm
double bridge to `fixed`.

## Decision

Use `fixed`'s deterministic `fixAtan2` and `fixSqrt` as specified in `docs/DESIGN.md` ("math library"):

- `atan2(y, x)` is `fixAtan2(y, x)`.
- `atan(x)` is `fixAtan2(x, MOO_NUMBER_ONE)`.
- `asin(x)` is `fixAtan2(x, fixSqrt(MOO_NUMBER_ONE - fixMul(x, x)))` for `|x| <= 1`
  (`-MOO_NUMBER_ONE <= x && x <= MOO_NUMBER_ONE`).
- `acos(x)` is `fixAtan2(fixSqrt(MOO_NUMBER_ONE - fixMul(x, x)), x)` for `|x| <= 1`.
- Out-of-domain inputs (`x < -MOO_NUMBER_ONE || x > MOO_NUMBER_ONE`) raise the named error
  `math.asin domain` for `math.asin` and `math.acos domain` for `math.acos`.

The library functions and the fastcalls share one definition (`moo_math_atan2`, `moo_math_atan`,
`moo_math_asin`, `moo_math_acos` in `VM/src/lmathlib.cpp`), so both paths produce identical bits. `asin`
and `acos` return `false` when `|x| > 1`, so the library raises `math.asin domain` / `math.acos domain` and
the fastcalls (`luauF_asin`, `luauF_acos`) return `-1` to fall back to the library and raise the same
error.

No new accuracy bound is claimed here beyond `fixed`'s documented `fixAtan2` bound (~0.0023 degrees, ~2.6
raw units): the `moo-l4-math-oracle` CORE-MATH vectors and error table are deferred to that card. The
reference in this card's tests is `fixed`'s C called directly.

## What changed

- `VM/src/lmathlib.cpp`:
  - `moo_math_atan2(fixed_t y, fixed_t x)` (new): returns `fixAtan2(y, x)`.
  - `moo_math_atan(fixed_t x)` (new): returns `fixAtan2(x, MOO_NUMBER_ONE)`.
  - `moo_math_asin(fixed_t x, fixed_t* out)` (new): returns `false` when
    `x < -MOO_NUMBER_ONE || x > MOO_NUMBER_ONE`, else sets `*out = fixAtan2(x, fixSqrt(MOO_NUMBER_ONE - fixMul(x, x)))`
    and returns `true`.
  - `moo_math_acos(fixed_t x, fixed_t* out)` (new): returns `false` when
    `x < -MOO_NUMBER_ONE || x > MOO_NUMBER_ONE`, else sets `*out = fixAtan2(fixSqrt(MOO_NUMBER_ONE - fixMul(x, x)), x)`
    and returns `true`.
  - `math_asin` (line ~143): calls `moo_math_asin(luaL_checknumber(L, 1), &out)` and raises
    `luaL_error(L, "math.asin domain")` when out of domain.
  - `math_acos` (line ~152): calls `moo_math_acos(luaL_checknumber(L, 1), &out)` and raises
    `luaL_error(L, "math.acos domain")` when out of domain.
  - `math_atan` (line ~161): calls `moo_math_atan(luaL_checknumber(L, 1))`.
  - `math_atan2` (line ~167): checks `y = luaL_checknumber(L, 1)` and `x = luaL_checknumber(L, 2)` in order
    and pushes `moo_math_atan2(y, x)`.
- `VM/src/lbuiltins.cpp`:
  - Forward declarations of `moo_math_atan2`, `moo_math_atan`, `moo_math_asin`, `moo_math_acos`.
  - `luauF_acos` (line ~71) and `luauF_asin` (line ~85): call `moo_math_acos` / `moo_math_asin` and return
    `-1` when out of domain so the VM falls back to the library call and raises `math.acos domain` /
    `math.asin domain`.
  - `luauF_atan2` (line ~99) and `luauF_atan` (line ~109): call `moo_math_atan2` and `moo_math_atan`
    directly on `nvalue` without `moo_numbridgetodouble` / `moo_numbridgefromdouble`.
- `tests/moo/moo-l4-math-inverse-trig.test.cpp` (new): doctest cases in `Moo.Test`.
  - `MathInverseTrigLibraryFastcallAndFixedC`: tests `asin` and `acos` across `[-1, 1]` (every boundary and
    interior step plus all 89-step raw points across `[-65536, 65536]`), `atan` across key, boundary,
    extremal (`INT64_MIN`, `INT64_MAX`), and 2048 seeded inputs, and `atan2` across all four quadrants, all
    eight axes/diagonals, `(0, 0)`, extremal combinations, and 2048 seeded pairs, verifying bit-exact
    agreement between the library path, fastcall path, and `fixed`'s C functions called directly.
  - `MathInverseTrigDomainErrors`: tests `math.asin` and `math.acos` outside `[-1, 1]` (`65537`, `-65537`,
    `1.5`, `-1.5`, `2`, `-2`, `100`, `-100`, `INT64_MAX`, `INT64_MIN`) on both the indirect library path
    and the bytecode fastcall path, verifying `math.asin domain` and `math.acos domain`.
  - `MathInverseTrigConformanceScript`: runs `l3/moo-l4-math-inverse-trig.luau`.
- `tests/conformance/moo/l3/moo-l4-math-inverse-trig.luau` (new): runs through the VM (`MooL3Scripts`) and
  pins exact raw values across `atan2` (all quadrants, axes, and `(0, 0)`), `atan`, `asin`, and `acos`, the
  `asin(x) + acos(x) == pi/2` identity on exact points, library vs fastcall agreement over a deterministic
  sweep, and the `math.asin domain` / `math.acos domain` errors at `1 + 1 raw` (`65537`), `-1 - 1 raw`
  (`-65537`), and larger out-of-domain inputs on both fastcall and library paths.

## DESIGN.md sentences that must change

- `docs/DESIGN.md` "Layer 3 as built", "TEMPORARY bridges": the libm bullet

  > the libm functions of `math`, `^`, `math.noise`, and the float `vector` type convert through doubles
  > (`moo_numbridgetodouble`, `moo_numbridgefromdouble`), until the math library is rebuilt on fixed and the
  > vector type is removed;

  no longer applies to `atan2`, `atan`, `asin`, or `acos`.
- `docs/DESIGN.md` "math library" table rows:

  > | `atan2` | `fixAtan2` |
  > | `asin`, `acos`, `atan` | `fixAtan2` with `fixSqrt`; out-of-domain input is a named error |

  already match this implementation; if the table later lists exact error strings, the named errors for
  `asin` and `acos` are `math.asin domain` and `math.acos domain`.

## DETERMINISM.md sentences that must change

- `docs/DETERMINISM.md` section 7 ("Platform libm in math, `^` and compile-time folding", lines ~212-217):

  > - `VM/src/lmathlib.cpp:47-200`: `sin`, `sinh`, `cos`, `cosh`, `tan`, `tanh`, `asin`,
  >   `acos`, `atan`, `atan2`, `sqrt` (`:136`), `pow` (`:142`), `log`/`log2`/`log10`
  >   (`:146-168`), `exp` (`:174`), `fmod`, `modf`, `frexp`, `ldexp`, and `round` (`:421`).
  > - `VM/src/lbuiltins.cpp:60` onward: the fastcall versions of the same functions (`acos`
  >   at `:65`, `atan2` at `:90`, `cos` at `:140`, `exp` at `:165`, `fmod` at `:192`, ...).

  must drop `asin`, `acos`, `atan`, and `atan2` from both the `VM/src/lmathlib.cpp` list and the
  `VM/src/lbuiltins.cpp` fastcall list, as they no longer cross the double bridge or call libm.
- `docs/DETERMINISM.md` "Status" row 7 (`platform libm`) remains `partly` until the remaining libm bridges
  and constant folding cards land.
