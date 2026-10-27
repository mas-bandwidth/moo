# moo-l4-pow-integer-vm

Layer 4's integer-exponent `^` in the VM: `luai_numpow` (`VM/src/lnumutils.h`) calls `moo_pow_int`
(`Common/include/moo_pow.h`, the design card's function) when the exponent is integer-valued, and a
fractional exponent stays on the libm bridge until `moo-l4-pow-fractional`. The function is unchanged.
This card's PATHS is `VM/src/lnumutils.h` only, so it changes just the seam and the tests; the opcode
callers (`VM/src/lvmexecute.cpp`, `VM/src/lvmutils.cpp`) are unchanged.

## Decision

No decision of this card's own. It follows `docs/layer4/moo-l4-pow-integer.md`: an exponent is integer-valued
when `moo_numisinteger(c)` holds, and the integer exponent is `c >> 16` (`MOO_NUMBER_FRACTION_BITS`).
`moo_numpow` (behind the `luai_numpow` macro) calls `moo_pow_int(a, b >> 16, &r)` and returns `r` — the
release value for `MOO_POW_OK` and `MOO_POW_SATURATED`, and 0 for `MOO_POW_DIVISION_BY_ZERO` (`0 ^ negative`).

The design note's named errors — `division by zero` on `MOO_POW_DIVISION_BY_ZERO` in every build, and
`number overflow in '^'` on `MOO_POW_SATURATED` under `#if MOO_OVERFLOW_ERRORS` — surface in the callers,
`luaV_doarithimpl<TM_POW>` and the `LOP_POW`/`LOP_POWK` opcodes. Those callers are in `VM/src/lvmutils.cpp`
and `VM/src/lvmexecute.cpp`, which are outside this card's PATHS; the closing card
(`moo-l4-conformance`) folds the full picture. This card pins the released bits.

The design card had not landed on `sprint/moo` at this card's original base, so its commits were carried on
this branch by cherry-pick; `moo_pow.h` and its note are not changed here.

## What changed

- `VM/src/lnumutils.h`:
  - includes `moo_pow.h`;
  - `moo_numpow` (new, behind the `luai_numpow` macro): an integer-valued `b` calls
    `moo_pow_int(a, b >> 16, &r)` and returns `r` (its release value, 0 for `0 ^ negative`); a fractional
    `b` keeps `moo_numbridgefromdouble(pow(...))`, with the TEMPORARY comment narrowed to that case;
  - the checked-arithmetic comment says `^` with an integer exponent is checked through `moo_pow_int`'s
    status.
- `tests/moo/moo-l4-pow-integer-vm.test.cpp` (new, suite `moo-l4-pow-integer-vm`):
  - `MooPowIntVmMatchesMooPowInt`: 2^12 seeded (base, integer exponent) pairs through both the two-operand
    `^` and the constant-exponent opcode, matched bit for bit to `moo_pow_int` (`*out`, which is the release
    value for OK and SATURATED and 0 for `0 ^ negative`);
  - `MooPowIntVmFractionalBridge`: a fractional exponent still gives the bridge's bits;
  - `MooPowIntVmZeroToNegative`: `0 ^ negative` releases 0 on both opcode paths and through a coerced string
    operand; `0 ^ 0 = 1` and `0 ^ positive = 0`.
- `tests/conformance/moo/l3/moo-l4-pow-integer.luau` (new): pins the note's exact raw values (`2 ^ 12`,
  `2 ^ 46`, the `2 ^ 47` saturation, `6 ^ -1 = 10923` against `1 / 6 = 10922`, `3 ^ -2 = 7282`, the
  `1.5 ^ 17` tie to even, negative bases) and `0 ^ negative` releasing 0.

## DESIGN.md sentences that must change

- "math library", "`pow` and `^`", the integer-valued bullet: replace "The target bound is under one raw
  unit for every result in range; layer 4 proves it by exhaustive test on small bases and the CORE-MATH
  comparison elsewhere." with "`moo_pow_int` (`Common/include/moo_pow.h`) does it. For `|n| <= 13` the
  power is an exact 256-bit integer. Above that it is bracketed between two 128-bit-mantissa values
  rounded down and up at every product, and a negative exponent takes the reciprocal of that bracket.
  Every result is correctly rounded (at most half a raw unit, ties to even). That is proved by visiting
  every input whose result is neither saturated nor 0 and finding the two bounds agree on all 76,620,463.
  `x ^ -1` therefore rounds where `1 / x` truncates."
- "Layer 3 as built", TEMPORARY bridges: "the libm functions of `math`, `^`, `math.noise`, ..." becomes
  "the libm functions of `math`, `^` and `math.pow` with a fractional exponent, `math.noise`, ...".
- "Overflow", as moo-l3-overflow-errors-debug's note rewrites it: "(`^` joins them when it is rebuilt on
  fixed)" becomes "and `^` with an integer exponent, by `moo_pow_int`'s status".
- "Division by zero": "`a / 0`, `a // 0` and `a % 0` raise the named error `division by zero`" becomes
  "`a / 0`, `a // 0`, `a % 0` and `0 ^ n` for an integer `n < 0` raise the named error `division by zero`".

## DETERMINISM.md sentences that must change

- The status table row 7: "the libm functions and `^` still cross a double bridge" becomes "`^` and
  `math.pow` with an integer exponent are integer arithmetic (`moo_pow_int`); the other libm functions and
  a fractional exponent still cross a double bridge".
- Section 7's bullet "`VM/src/lnumutils.h:12` `luai_numpow` is `pow` ..." gains "for a fractional exponent
  only".

## Not done

- The opcode callers (`VM/src/lvmexecute.cpp` `LOP_POW`/`LOP_POWK` and `VM/src/lvmutils.cpp`
  `luaV_doarithimpl`), which raise `division by zero` on `0 ^ negative` and, in a checked build,
  `number overflow in '^'` on saturation, are outside this card's PATHS and are not changed. `0 ^ negative`
  therefore releases 0 but does not raise on this branch.
- `tests/moo/moo-l3-overflow-errors-debug.test.cpp`'s `MooOverflowPowUncheckedBridge` (which pins `^` as
  the unchecked libm bridge) is unchanged; the design note's allowance to replace it waits for the card that
  moves the error raising into the callers.
- `math.pow`, its fastcall and constant folding are the other two pro cards; this card touches only the `^`
  VM seam.
- `Common/include/moo_number.h`'s saturation comment still says `^` is not checked; that file is outside this
  card's PATHS, so the change waits for the closing card.
