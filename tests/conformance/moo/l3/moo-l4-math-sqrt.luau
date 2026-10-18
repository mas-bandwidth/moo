-- moo: layer 4's sqrt, through the VM. math.sqrt is fixed's fixSqrt: exact, rounded toward zero, and a
-- negative input is the named error (docs/DESIGN.md, "math library": sqrt from fixSqrt). The exhaustive
-- pool is in tests/moo/moo-l4-math-sqrt.test.cpp; this file pins the rule on concrete raw values
-- (moo_test.fromraw builds a number from its raw Q48.16 value; moo_test.raw reports one).
local function sq(x)
    return moo_test.raw(math.sqrt(moo_test.fromraw(x)))
end

assert(sq("0") == "0")              -- sqrt(0) = 0
assert(sq("65536") == "65536")      -- sqrt(1) = 1
assert(sq("131072") == "92681")     -- sqrt(2)
assert(sq("196608") == "113511")    -- sqrt(3), toward zero
assert(sq("262144") == "131072")    -- sqrt(4) = 2
assert(sq("16384") == "32768")      -- sqrt(0.25) = 0.5
assert(sq("32768") == "46340")      -- sqrt(0.5)
assert(sq("6554") == "20724")       -- sqrt(0.1)
assert(sq("21845") == "37836")      -- sqrt(1/3)
assert(sq("98304") == "80264")      -- sqrt(1.5)

-- a negative input is the named error on both the direct call (fastcall) and an alias call
local ok, err = pcall(function() return math.sqrt(-1) end)
assert(not ok)
assert(tostring(err):find("math.sqrt of a negative number"), tostring(err))

local s = math.sqrt
local ok2, err2 = pcall(function() return s(-4) end)
assert(not ok2)
assert(tostring(err2):find("math.sqrt of a negative number"), tostring(err2))

return "OK"
