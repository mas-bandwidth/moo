-- moo: checked arithmetic (docs/DESIGN.md, "Overflow"). Release saturates; a checked build (MOO_OVERFLOW_ERRORS,
-- on with assertions) raises "number overflow in '<op>'" instead, so a program either gives the release value or
-- stops with the named error. This script runs in both builds: the first edge says which one this is, and every
-- other operator must agree with it. tests/moo/moo-l3-overflow-errors-debug.test.cpp pins each build by name.
local max = moo_test.fromraw("9223372036854775807") -- MOO_NUMBER_MAX, 2^47 - 2^-16
local min = moo_test.fromraw("-9223372036854775807") -- MOO_NUMBER_MIN
local step = moo_test.fromraw("1") -- 2^-16
local big = moo_test.fromraw("4611686018427387904") -- 2^46

local checked = not pcall(function(a, b) return a + b end, max, step)

-- an operation past the edge: release saturates to want, a checked build raises the named error
local function edge(name, want, fn, ...)
    local ok, result = pcall(fn, ...)
    if checked then
        assert(not ok, name .. " did not raise")
        local suffix = "number overflow in '" .. name .. "'"
        assert(string.sub(result, -#suffix) == suffix, result)
    else
        assert(ok, result)
        assert(result == want, name .. ": " .. moo_test.raw(result))
    end
end

-- one step inside the range: both builds give want
local function inside(name, want, fn, ...)
    local ok, result = pcall(fn, ...)
    assert(ok, name .. ": " .. tostring(result))
    assert(result == want, name .. ": " .. moo_test.raw(result))
end

-- runtime operands (registers and constants), so nothing folds
edge("+", max, function(a, b) return a + b end, max, step)
edge("+", max, function(a) return a + 0.0000152587890625 end, max)
edge("+", min, function(a, b) return a + b end, min, -step)
inside("+", max, function(a, b) return a + b end, max - step, step)

edge("-", min, function(a, b) return a - b end, min, step)
edge("-", min, function(a) return a - 0.0000152587890625 end, min)
edge("-", max, function(a) return 1 - a end, min)
inside("-", max, function(a) return 1 - a end, min + 1)
inside("-", min, function(a, b) return a - b end, min + step, step)

edge("*", max, function(a, b) return a * b end, max, 1 + step)
edge("*", max, function(a) return a * 2 end, big)
edge("*", min, function(a) return a * -2 end, big)
inside("*", max, function(a, b) return a * b end, max, 1)
inside("*", max - step, function(a) return a * 2 end, big - step)

edge("/", max, function(a, b) return a / b end, max, 1 - step)
edge("/", max, function(a) return a / 0.5 end, big)
edge("/", max, function(a) return 2147483648 / a end, step)
edge("/", min, function(a, b) return a / b end, big, -0.5)
inside("/", max, function(a, b) return a / b end, max, 1)
inside("/", big, function(a) return 2147483648 / a end, step + step)

edge("//", max, function(a, b) return a // b end, max, 1 - step)
edge("//", max, function(a) return a // 0.5 end, big)
edge("//", min, function(a, b) return a // b end, big, -0.5)
inside("//", 140737488355327, function(a, b) return a // b end, max, 1)
inside("//", 140737488355327, function(a) return a // 0.5 end, big - step)

edge("^", max, function(a, b) return a ^ b end, 2, 47)
edge("^", max, function(a) return a ^ 47 end, 2)
edge("^", min, function(a) return a ^ 47 end, -2)
inside("^", 70368744177664, function(a) return a ^ 46 end, 2)

-- the string coercion path (luaV_doarith) raises the same error
edge("+", max, function(a, b) return a + b end, "140737488355327", max)

-- unary - and % are exact on the domain and never saturate, in either build
inside("unary -", min, function(a) return -a end, max)
inside("unary -", max, function(a) return -a end, min)
inside("%", max - step, function(a, b) return a % b end, max - step, max)
inside("%", -step, function(a, b) return a % b end, max - step, -max)
inside("%", 0, function(a, b) return a % b end, min, step)

-- folded constants: a checked build keeps a saturating operation for run time, so it raises there too
edge("+", max, function() return 140737488355327 + 1 end)
edge("-", min, function() return -140737488355327 - 1 end)
edge("*", max, function() return 70368744177664 * 2 end)
edge("/", max, function() return 70368744177664 / 0.5 end)
edge("//", max, function() return 70368744177664 // 0.5 end)
edge("^", max, function() return 2 ^ 47 end)
inside("+", 1073741825, function() return 1073741824 + 1 end)
inside("*", 2147483648, function() return 1073741824 * 2 end)

return "OK"
