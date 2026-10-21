-- moo: integer-exponent pow in the math library (docs/DESIGN.md "math library", "pow and ^"): math.pow
-- and its fastcall call moo_pow_int, so an integer-valued exponent is exact and rounded once, ties to
-- even, saturating in release; 0 ^ negative is "division by zero"; a fractional exponent stays on the
-- libm bridge (docs/layer4/moo-l4-pow-integer.md).

local one = moo_test.fromraw("65536")

local function rawPow(x, n)
    return moo_test.raw(math.pow(x, n))
end

-- Exact powers, including 0 ^ 0, and the sign of an odd exponent on a negative base.
assert(rawPow(moo_test.fromraw("131072"), 10 * one) == "67108864", "2 ^ 10")
assert(rawPow(moo_test.fromraw("0"), 0) == "65536", "0 ^ 0")
assert(rawPow(moo_test.fromraw("0"), 3 * one) == "0", "0 ^ 3")
assert(rawPow(moo_test.fromraw("-131072"), 3 * one) == "-524288", "(-2) ^ 3")

-- Negative exponents round once, half to even: 6 ^ -1 is 10923 (1 / 6 truncates to 10922) and
-- 3 ^ -2 is 7282 (the rounding of 65536 / 9).
assert(rawPow(moo_test.fromraw("393216"), -one) == "10923", "6 ^ -1")
assert(rawPow(moo_test.fromraw("196608"), -2 * one) == "7282", "3 ^ -2")

-- 0 ^ negative is the named error, on the library and the fastcall path.
local libraryPow = math.pow
for _, call in ipairs({
    function() return math.pow(moo_test.fromraw("0"), -one) end,
    function() return libraryPow(moo_test.fromraw("0"), -one) end,
}) do
    local ok, err = pcall(call)
    assert(not ok, "0 ^ -1 must raise")
    assert(string.find(err, "division by zero", 1, true), "0 ^ -1: " .. tostring(err))
end

-- Release saturates: 2 ^ 47 and (-2) ^ 47 land on the edges.
assert(rawPow(moo_test.fromraw("131072"), 47 * one) == moo_test.raw(math.huge), "2 ^ 47 saturates")
assert(rawPow(moo_test.fromraw("-131072"), 47 * one) == moo_test.raw(-math.huge), "(-2) ^ 47 saturates")

-- The library path (a local alias, no fastcall) and the fastcall path agree on a small sweep.
local library = math.pow
for n = -8, 8 do
    for _, baseRaw in ipairs({"0", "65536", "-65536", "98304", "393216", "-196608"}) do
        local base = moo_test.fromraw(baseRaw)
        local exponent = n * one
        if baseRaw ~= "0" or n >= 0 then
            assert(moo_test.raw(library(base, exponent)) == rawPow(base, exponent),
                "library vs fastcall: " .. baseRaw .. " ^ " .. n)
        end
    end
end

-- moo: a fractional exponent is fixPow: 4 ^ 0.5 is exact, and 9 ^ 1.5 is 1769466 as fixPow computes it.
assert(rawPow(moo_test.fromraw("262144"), 0.5) == "131072", "4 ^ 0.5")
-- moo: fixPow's value is pinned in moo-l4-pow-fractional.luau; 27 (1769472) was the libm bridge's.
assert(rawPow(moo_test.fromraw("589824"), 1.5) == "1769466", "9 ^ 1.5")

return "OK"
