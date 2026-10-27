-- This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
-- moo: The number oracle: every operator, literal path, boundary function and integer-argument rule checked
-- bit for bit against fixed's C (tests/moo/moo_oracle_fixed.c; docs/DESIGN.md "Conformance (layer 3)").
-- Each block below is one rule; MooSpecScripts runs this file and expects "OK".

do
-- moo: the script side of the layer 3 oracle (docs/history/layer3/moo-l3-oracle-harness.md). The bit-exact sweep against
-- fixed's C is tests/moo/oracle.test.cpp (doctest suite moo-oracle); this file pins, in moo itself,
-- the cases a script author meets first: the folded and the run-time result of each operator are the same raw
-- value, ties of `*` round half up, -0 is 0, saturation, floor signs, and division by zero is the named error.

local function raw(x)
	return moo_test.raw(x)
end

-- run-time operands: values the compiler cannot see (DESIGN.md "Layer 3 as built", the compiler folds constants)
local function rt(s)
	return moo_test.fromraw(s)
end

local MAX = rt("9223372036854775807")
local MIN = rt("-9223372036854775807")
local u = rt("1") -- one raw unit

-- the folded and the run-time result agree, operator by operator (literals first, then the VM on the same raws;
-- that literal expressions like these fold is pinned by OracleLiteralPathsFoldAndReachKOpcodes)
assert(raw(1.5 + 0.25) == raw(rt("98304") + rt("16384")))
assert(raw(1.5 - 0.25) == raw(rt("98304") - rt("16384")))
assert(raw(1.5 * 0.25) == raw(rt("98304") * rt("16384")))
assert(raw(1.5 / 0.25) == raw(rt("98304") / rt("16384")))
assert(raw(-7.5 // 2) == raw(rt("-491520") // rt("131072")))
assert(raw(-7.5 % 2) == raw(rt("-491520") % rt("131072")))
assert(raw(-(1.5)) == raw(-rt("98304")))

-- `*` rounds half up (DESIGN.md "Semantics"): one raw unit times 0.5 drops exactly half
assert(raw(u * 0.5) == "1")
assert(raw(-u * 0.5) == "0")
assert(raw(rt("3") * 0.5) == "2")
assert(raw(rt("-3") * 0.5) == "-1")

-- `/` truncates toward zero; `//` floors; `%` takes the sign of the divisor
assert(raw(u / 2) == "0" and raw(-u / 2) == "0")
assert(raw(-7 // 2) == "-262144" and raw(7 // -2) == "-262144")
assert(raw(-7 % 2) == "65536" and raw(7 % -2) == "-65536")

-- there is no negative zero (DESIGN.md "Representation")
assert(raw(-0) == "0")
assert(-0 == 0)
assert(raw(0 * -1) == "0")

-- saturation, never wrap (DESIGN.md "Overflow"); a checked build (MOO_OVERFLOW_ERRORS) raises the named error
-- instead (tests/conformance/moo/spec/number.luau); unary minus of MIN is exact, never checked
local function saturates(fn, want, name)
	local ok, result = pcall(fn)
	local suffix = "number overflow in '" .. name .. "'"
	assert((ok and raw(result) == want) or (not ok and string.sub(tostring(result), -#suffix) == suffix), tostring(result))
end
saturates(function() return MAX + u end, "9223372036854775807", "+")
saturates(function() return MIN - u end, "-9223372036854775807", "-")
assert(raw(-MIN) == "9223372036854775807")
saturates(function() return MAX * 2 end, "9223372036854775807", "*")
saturates(function() return MIN * 2 end, "-9223372036854775807", "*")
saturates(function() return MAX / u end, "9223372036854775807", "/")
saturates(function() return MAX // u end, "9223372036854775807", "//")

-- division by zero is the named error, folded or not (DESIGN.md "Division by zero")
local zero = rt("0")
for _, f in { function() return 1 / 0 end, function() return 1 // 0 end, function() return 1 % 0 end,
	function() return u / zero end, function() return u // zero end, function() return u % zero end,
	function() return 1 / -0 end } do
	local ok, err = pcall(f)
	assert(not ok and string.find(tostring(err), "division by zero", 1, true), tostring(err))
end
end

return "OK"
