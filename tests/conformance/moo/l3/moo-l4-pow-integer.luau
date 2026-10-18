-- moo: DESIGN.md "math library", "pow and ^": ^ with an integer-valued exponent is moo_pow_int, exact and
-- rounded once, ties to even, saturating; 0 ^ negative releases 0. These are concrete raw values from
-- docs/layer4/moo-l4-pow-integer.md, run through the VM.
local fromraw = moo_test.fromraw
local raw = moo_test.raw

-- x ^ 0 = 1 for every x, 0 included; 0 ^ positive = 0.
assert(raw(fromraw('0') ^ 0) == '65536')
assert(raw(fromraw('1234567') ^ 0) == '65536')
assert(raw(fromraw('0') ^ 2) == '0')
assert(raw(fromraw('0') ^ 46) == '0')

-- Exact integer powers: 2 ^ 12 and 2 ^ 46 are in range, 2 ^ 47 saturates in release.
assert(raw(2 ^ 12) == '268435456')
assert(raw(2 ^ 46) == '4611686018427387904')
assert(raw(2 ^ 47) == '9223372036854775807')

-- A negative exponent rounds once, half to even: 6 ^ -1 is 10923 where 1 / 6 truncates to 10922.
assert(raw(fromraw('393216') ^ -1) == '10923')
assert(raw(fromraw('65536') / fromraw('393216')) == '10922')
assert(raw(fromraw('196608') ^ -2) == '7282')

-- The exact tie 1.5 ^ 17 = 3 ^ 17 / 2 rounds to even.
assert(raw(fromraw('98304') ^ 17) == '64570082')

-- A negative base keeps its sign for an odd exponent.
assert(raw(fromraw('-131072') ^ 3) == '-524288')
assert(raw(fromraw('-131072') ^ 2) == '262144')

-- 0 ^ negative releases 0 (moo_pow_int's *out on DIVISION_BY_ZERO), through the VM and through a coerced
-- operand. The named error "division by zero" surfaces in the opcode callers, which are outside this card's
-- PATHS.
assert(raw(fromraw('0') ^ -1) == '0')
assert(raw(fromraw('0') ^ fromraw('-65536')) == '0')
assert(raw('0' ^ fromraw('-65536')) == '0')

return "OK"
