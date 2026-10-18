-- moo: DESIGN.md "math library", "pow and ^": a non-integer exponent is fixPow = fixExp2(exponent *
-- fixLog2(base)); a negative base is the named error "pow of a negative number to a fractional power";
-- 0 ^ positive fraction = 0. These run through the VM. Operands come from moo_test.fromraw so nothing
-- constant-folds.
local fromraw = moo_test.fromraw
local raw = moo_test.raw

-- 0 ^ positive fraction = 0, on both the opcode and math.pow paths.
assert(raw(fromraw('0') ^ 32768) == '0')
assert(raw(fromraw('0') ^ 98304) == '0')
assert(raw(math.pow(fromraw('0'), fromraw('32768'))) == '0')

-- Exact fractional powers: 4 ^ 0.5 = 2, 9 ^ 0.5 = 3 (fixPow is exact here).
assert(raw(fromraw('262144') ^ fromraw('32768')) == '131072')
assert(raw(fromraw('589824') ^ fromraw('32768')) == '196608')
assert(raw(math.pow(fromraw('262144'), fromraw('32768'))) == '131072')
assert(raw(math.pow(fromraw('589824'), fromraw('32768'))) == '196608')

-- Near-exact fractional powers: 2 ^ 0.5, 3 ^ 0.5, 6 ^ 0.5, 8 ^ 0.5 as fixPow computes them.
assert(raw(fromraw('131072') ^ fromraw('32768')) == '92682')
assert(raw(fromraw('196608') ^ fromraw('32768')) == '113512')
assert(raw(fromraw('393216') ^ fromraw('32768')) == '160530')
assert(raw(fromraw('524288') ^ fromraw('32768')) == '185364')

-- A non-trivial fractional power: 9 ^ 1.5 as fixPow computes it (1769466; the ideal 27 is 1769472).
assert(raw(fromraw('589824') ^ fromraw('98304')) == '1769466')
assert(raw(math.pow(fromraw('589824'), fromraw('98304'))) == '1769466')

-- 1 ^ anything = 1.
assert(raw(fromraw('65536') ^ fromraw('32768')) == '65536')
assert(raw(math.pow(fromraw('65536'), fromraw('98304'))) == '65536')

-- A negative base with a fractional exponent is the named error in math.pow; the opcode releases fixPow's 0.
local ok, err = pcall(function() return math.pow(-1, fromraw('32768')) end)
assert(not ok and tostring(err):find('pow of a negative number to a fractional power', 1, true))
local ok2, err2 = pcall(function() return math.pow(-4, fromraw('-98304')) end)
assert(not ok2 and tostring(err2):find('pow of a negative number to a fractional power', 1, true))
assert(raw(fromraw('-65536') ^ fromraw('32768')) == '0')

-- An integer exponent still takes moo_pow_int, no error.
assert(raw(math.pow(fromraw('-131072'), fromraw('196608'))) == '-524288')
assert(raw(fromraw('-131072') ^ 3) == '-524288')

return "OK"
