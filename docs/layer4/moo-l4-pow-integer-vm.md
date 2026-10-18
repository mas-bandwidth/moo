# moo-l4-pow-integer-vm

Layer 4's integer-exponent `^` in the VM: `luai_numpow` and the `^` opcodes call `moo_pow_int`
(`Common/include/moo_pow.h`, the design card's function) when the exponent is integer-valued, and a
fractional exponent stays on the libm bridge until `moo-l4-pow-fractional`. The function is unchanged.

## Decision

No decision of this card's own. It follows `docs/layer4/moo-l4-pow-integer.md`: an exponent is
integer-valued when `moo_numisinteger(c)` holds, and the integer exponent is `c >> 16`
(`MOO_NUMBER_FRACTION_BITS`). `moo_pow_int`'s status decides the named errors: `MOO_POW_DIVISION_BY_ZERO`
raises `division by zero` in every build, and `MOO_POW_SATURATED` raises `number overflow in '^'` under
`#if MOO_OVERFLOW_ERRORS`; release uses its `*out` for `MOO_POW_OK` and `MOO_POW_SATURATED`.

The design card had not landed on `sprint/moo` at this card's base (`6285e3f6`), so its two commits
(`29e1432e`, `6178c9f1`) are carried on this branch by cherry-pick; `moo_pow.h` and its note are not
changed here.

## What changed

- `VM/src/lnumutils.h`:
  - includes `moo_pow.h`;
  - `moo_numpow` (new, behind the `luai_numpow` macro): an integer-valued `b` calls
    `moo_pow_int(a, b >> 16, &r)` and returns `r` (its release value, 0 for `0 ^ negative`); a fractional
    `b` keeps `moo_numbridgefromdouble(pow(...))`, with the TEMPORARY comment narrowed to that case;
  - the checked-arithmetic comment says `^` with an integer exponent is checked through `moo_pow_int`'s
    status.
- `VM/src/lvmexecute.cpp` `LOP_POW` and `LOP_POWK`: the fast path calls `moo_pow_int` once for an
  integer-valued exponent. On `MOO_POW_DIVISION_BY_ZERO`, and on `MOO_POW_SATURATED` under
  `#if MOO_OVERFLOW_ERRORS`, it leaves for `luaV_doarithimpl<TM_POW>`, which raises; otherwise it sets
  `*out`. A fractional exponent calls `luai_numpow` (the bridge).
- `VM/src/lvmutils.cpp` `luaV_doarithimpl`: for `TM_POW` with an integer-valued exponent it calls
  `moo_pow_int` once, raises `division by zero` on its division-by-zero status beside the `/`, `//` and `%`
  check, and raises `number overflow in '^'` on its saturation status in the existing
  `#if MOO_OVERFLOW_ERRORS` block. The `TM_POW` case uses the computed value; a fractional exponent uses
  `luai_numpow`.
- `tests/moo/moo-l4-pow-integer-vm.test.cpp` (new, suite `moo-l4-pow-integer-vm`):
  - `MooPowIntVmMatchesMooPowInt`: 2^12 seeded (base, integer exponent) pairs through both the
    two-operand `^` and the constant-exponent opcode, matched to `moo_pow_int`'s status and bits;
  - `MooPowIntVmFractionalBridge`: a fractional exponent still gives the bridge's bits;
  - `MooPowIntVmZeroToNegativeRaises`: `0 ^ negative` raises `division by zero` on both opcode paths and
    through a coerced string operand, and `0 ^ 0` and `0 ^ positive` do not.
- `tests/conformance/moo/l3/moo-l4-pow-integer.luau` (new): pins the note's exact raw values
  (`2 ^ 12`, `2 ^ 46`, the `2 ^ 47` saturation, `6 ^ -1 = 10923` against `1 / 6 = 10922`,
  `3 ^ -2 = 7282`, the `1.5 ^ 17` tie to even, negative bases) and the named error.
- `tests/moo/moo-l3-overflow-errors-debug.test.cpp`: `MooOverflowPowUncheckedBridge` pinned behaviour this
  card removes, so it becomes `MooOverflowPowInteger`: `2 ^ 47` saturates in release and raises
  `number overflow in '^'` in a checked build; `2 ^ 46` stays exact. (The design note allows this edit.)

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
  "`a / 0`, `a // 0`, `a % 0` and `0 ^ n` for an integer `n < 0` raise the named error `division by
  zero`".

## DETERMINISM.md sentences that must change

- The status table row 7: "the libm functions and `^` still cross a double bridge" becomes "`^` and
  `math.pow` with an integer exponent are integer arithmetic (`moo_pow_int`); the other libm functions and
  a fractional exponent still cross a double bridge".
- Section 7's bullet "`VM/src/lnumutils.h:12` `luai_numpow` is `pow` ..." gains "for a fractional exponent
  only".

## Not done

- `math.pow`, its fastcall and constant folding are the other two pro cards; this card touches only the
  `^` VM path.
- `Common/include/moo_number.h:105-107`'s saturation comment still says `^` is not checked; this card's
  path list does not include that file, so the change waits for the closing card.
- The design card's commits are carried here because they had not landed on the base.
