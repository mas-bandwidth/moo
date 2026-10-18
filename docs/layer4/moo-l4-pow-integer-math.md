# moo-l4-pow-integer-math

`math.pow` and its bytecode fastcall now call `moo_pow_int` from `Common/include/moo_pow.h` when the
second argument is integer-valued. The core function, its proof and the design are the design card's
(`docs/layer4/moo-l4-pow-integer.md`, `Common/include/moo_pow.h`); this card changes neither.

## Decision

None. The note for this card in `docs/layer4/moo-l4-pow-integer.md` ("The pro cards", "moo-l4-pow-integer-math")
fixes the rule: raise `division by zero` on `MOO_POW_DIVISION_BY_ZERO`, raise `number overflow in '^'` on
`MOO_POW_SATURATED` only under `#if MOO_OVERFLOW_ERRORS`, and otherwise push `*out`; the fastcall returns
`-1` instead of raising so the call falls back to `math_pow`. A fractional exponent stays on the libm
bridge until `moo-l4-pow-fractional`.

## What changed

- `VM/src/lmathlib.cpp`, `math_pow`: include `moo_pow.h`; when `moo_numisinteger(exponent)` call
  `moo_pow_int(base, exponent >> MOO_NUMBER_FRACTION_BITS, &out)`, raise the two named errors as above,
  and push the raw result. The non-integer case is the unchanged `pow` bridge, now in its own branch.
- `VM/src/lbuiltins.cpp`, `luauF_pow`: include `moo_pow.h`; the same integer test calls the same
  `moo_pow_int`, sets the raw result on `MOO_POW_OK`, and returns `-1` on `MOO_POW_DIVISION_BY_ZERO` and,
  under `#if MOO_OVERFLOW_ERRORS`, on `MOO_POW_SATURATED`, so `math_pow` raises. The fastcall never raises.
  A fractional exponent keeps the `pow` bridge.
- NEW `tests/moo/moo-l4-pow-integer-math.test.cpp` (suite `moo-l4-pow-integer-math` in `Moo.Test`):
  - `MathPowIntegerMatchesCoreOnSeededPairs`: 2^12 seeded (base, integer exponent) pairs, a quarter of the
    bases within 2 of 1 and every 13th exponent on the interval path, with and without the fastcall
    (a local alias prevents it), all equal to `moo_pow_int`'s value or named error.
  - `MathPowFractionalStaysOnBridge`: fractional exponents equal the host `pow` through the boundary
    rounding on both paths.
  - `MathPowIntegerEdges`: `0 ^ 0`, `0 ^ positive`, `0 ^ negative`, `MAX ^ 1` (exact, not saturation),
    `6 ^ -1 = 10923`, `3 ^ -2 = 7282`, and `+-2 ^ 47` (saturating edges, or the checked error).
- NEW `tests/conformance/moo/l3/moo-l4-pow-integer-math.luau`: `math.pow` on integer exponents (exact
  powers, negative exponents, the two named errors, saturation, library vs fastcall) and a fractional
  exponent on the bridge; returns `"OK"`.

## DESIGN.md and DETERMINISM.md sentences that must change

On this card's landing (the same sentences `moo-l4-pow-integer-vm` names, because both pro cards remove
the integer exponent from the bridge):

- DESIGN.md, "Layer 3 as built", TEMPORARY bridges: "the libm functions of `math`, `^`, `math.noise`, ..."
  becomes "the libm functions of `math`, `^` and `math.pow` with a fractional exponent, `math.noise`, ..."
- DETERMINISM.md, the status table row 7: "the libm functions and `^` still cross a double bridge" becomes
  "`^` and `math.pow` with an integer exponent are integer arithmetic (`moo_pow_int`); the other libm
  functions and a fractional exponent still cross a double bridge".
- DETERMINISM.md, section 7, the bullet "`VM/src/lnumutils.h:12` `luai_numpow` is `pow` ..." gains "for a
  fractional exponent only"; `VM/src/lmathlib.cpp` `math_pow` and `VM/src/lbuiltins.cpp` `luauF_pow` cross
  the bridge only for a fractional exponent.

The "math library", "`pow` and `^`" integer-valued bullet is the design card's replacement and is not
repeated here.

## Not done

- Nothing from this card. The `^` opcode paths, the compiler folding and the fractional exponent are other
  cards (`moo-l4-pow-integer-vm`, `-fold`, `-fractional`).
