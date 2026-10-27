# moo-l4-math-trig

`math.sin`, `math.cos` and `math.tan` (and their bytecode fastcalls) move from the libm double bridge to
fixed.

## Decision

Use `fixed`'s deterministic transcendentals as designed:

- `sin` and `cos` come from `fixComputeCosSin` (`fixSin` / `fixCos`).
- `tan` is `sin / cos`; a zero cosine is the named error `division by zero`.

The library functions and the fastcalls share one definition (`moo_math_sin`, `moo_math_cos`,
`moo_math_tan` in `VM/src/lmathlib.cpp`), so both paths give the same bits. `tan` reports a zero cosine by
returning false, so the library raises `division by zero` and the fastcall returns -1 to fall back to the
library and raise the same error. This is the mechanism layer 3 established for fastcall error handling
(`moo-l3-integer-args-fastcalls`), which the math fastcalls keep.

No accuracy bound is claimed here: the `moo-l4-math-oracle` vectors have not landed, so the CORE-MATH
maximum-error table is deferred to that card. The reference in this card's tests is `fixed`'s C called
directly.

## What changed

- `VM/src/lmathlib.cpp`:
  - `moo_math_sin` (new): `fixSin(radians)`.
  - `moo_math_cos` (new): `fixCos(radians)`.
  - `moo_math_tan` (new): `fixComputeCosSin(radians)`; returns false when the cosine is zero, else
    `moo_numdiv(sine, cosine)`.
  - `math_sin` (line ~110): `lua_pushnumber(L, moo_math_sin(luaL_checknumber(L, 1)))`.
  - `math_cos`: same through `moo_math_cos`.
  - `math_tan`: raises `division by zero` when `moo_math_tan` returns false, else pushes the quotient.
- `VM/src/lbuiltins.cpp`:
  - forward declarations of `moo_math_sin` / `moo_math_cos` / `moo_math_tan`;
  - `luauF_cos` (line ~142), `luauF_sin` (line ~387), `luauF_tan` (line ~425) call the shared helpers, and
    `luauF_tan` returns -1 on a zero cosine so the fallback raises `division by zero`.
- `tests/moo/moo-l4-math-trig.test.cpp` (new): doctest cases in `Moo.Test`.
  - `MathTrigLibraryFastcallBitExact`: library vs fastcall give the same bits for every input.
  - `MathTrigMatchesFixedC`: the library matches `fixed`'s C called directly.
  - `MathTrigDivisionByZero`: `tan(pi/2)` and `tan(-pi/2)` raise `division by zero` on both paths.
  - The input pool is the multiples of pi/2, zero, the domain limits (`INT64_MAX`, `-INT64_MAX`), large
    values, and 4096 seeded raw values.
- `tests/conformance/moo/l3/moo-l4-math-trig.luau` (new): runs through the VM (`MooL3Scripts`) and pins
  the values and the `division by zero` error on concrete raw values, on the fastcall and the library path.

## DESIGN.md sentences that must change

- `docs/DESIGN.md` "Layer 3 as built", "TEMPORARY bridges": the libm bullet currently reads

  > the libm functions of `math`, `^`, `math.noise`, and the float `vector` type convert through doubles
  > (`moo_numbridgetodouble`, `moo_numbridgefromdouble`), until the math library is rebuilt on fixed and the
  > vector type is removed;

  It must no longer name `sin`, `cos` and `tan`, which this card rebuilds on fixed.
- `docs/DESIGN.md` "math library" table already names `sin`/`cos` → `fixSin`, `fixCos` (`fixComputeCosSin`)
  and `tan` → `sin / cos`; a zero cosine is `division by zero`. It holds as implemented, so no edit is
  required to the table rows.

## DETERMINISM.md sentences that must change

- `docs/DETERMINISM.md` section 7 fix (line ~212):

  > `VM/src/lmathlib.cpp:47-200`: `sin`, `sinh`, `cos`, `cosh`, `tan`, `tanh`, `asin`, `acos`, `atan`,
  > `atan2`, `sqrt`, `pow`, `log`/`log2`/`log10`

  and the fastcall list (`VM/src/lbuiltins.cpp:60` onward naming `cos` and `tan`) must drop `sin`, `cos`
  and `tan`, which no longer cross the bridge; the remaining libm functions stay until their own cards.
- `docs/DETERMINISM.md` "Status" row 7 (platform libm) stays "partly" until the rest of the math library
  lands.
