-- moo: DESIGN.md "math library": math.atan2, math.atan, math.asin and math.acos on fixed.
-- atan2 is fixAtan2, atan(x) is fixAtan2(x, 1), asin(x) is fixAtan2(x, fixSqrt(1 - x*x)) and
-- acos(x) is fixAtan2(fixSqrt(1 - x*x), x); |x| > 1 raises "math.asin domain" / "math.acos domain"
-- on both the library and fastcall paths.
local f_asin = math.asin
local f_acos = math.acos
local f_atan = math.atan
local f_atan2 = math.atan2

local raw = moo_test.raw
local fromraw = moo_test.fromraw

local one = fromraw("65536")
local half = fromraw("32768")
local overOne = fromraw("65537")
local underNegOne = fromraw("-65537")

-- Exact points on both fastcall and library paths
assert(raw(math.asin(0)) == "0" and raw(f_asin(0)) == "0")
assert(raw(math.asin(one)) == "102944" and raw(f_asin(one)) == "102944")
assert(raw(math.asin(-one)) == "-102944" and raw(f_asin(-one)) == "-102944")

assert(raw(math.acos(one)) == "0" and raw(f_acos(one)) == "0")
assert(raw(math.acos(0)) == "102944" and raw(f_acos(0)) == "102944")
assert(raw(math.acos(-one)) == "205887" and raw(f_acos(-one)) == "205887")

assert(raw(math.atan(0)) == "0" and raw(f_atan(0)) == "0")
assert(raw(math.atan(one)) == "51474" and raw(f_atan(one)) == "51474")
assert(raw(math.atan(-one)) == "-51474" and raw(f_atan(-one)) == "-51474")

assert(raw(math.atan2(0, 0)) == "0" and raw(f_atan2(0, 0)) == "0")
assert(raw(math.atan2(0, one)) == "0" and raw(f_atan2(0, one)) == "0")
assert(raw(math.atan2(one, 0)) == "102944" and raw(f_atan2(one, 0)) == "102944")
assert(raw(math.atan2(-one, 0)) == "-102944" and raw(f_atan2(-one, 0)) == "-102944")
assert(raw(math.atan2(0, -one)) == "205887" and raw(f_atan2(0, -one)) == "205887")

-- Agreement between fastcall and library at interior values (where fixed differs by 1-2 raw from libm)
for _, x in ipairs{-one, -half, fromraw("-12345"), 0, fromraw("12345"), half, one} do
    assert(math.asin(x) == f_asin(x), "asin fastcall vs library")
    assert(math.acos(x) == f_acos(x), "acos fastcall vs library")
    assert(math.atan(x) == f_atan(x), "atan fastcall vs library")
end
-- At raw 12345, fixAtan2(12345, fixSqrt(65536 - fixMul(12345, 12345))) = 12420 (libm bridge gave 12419)
assert(raw(math.asin(fromraw("12345"))) == "12420")
assert(raw(f_asin(fromraw("12345"))) == "12420")

-- Out-of-domain |x| > 1 raises "math.asin domain" / "math.acos domain" on both fastcall and library paths
for _, bad in ipairs{overOne, underNegOne, 2, -2, math.huge, -math.huge} do
    local ok1, err1 = pcall(function() return math.asin(bad) end)
    assert(not ok1 and string.find(err1, "math.asin domain", 1, true) ~= nil, "fastcall asin(" .. tostring(bad) .. "): " .. tostring(err1))
    local ok2, err2 = pcall(f_asin, bad)
    assert(not ok2 and string.find(err2, "math.asin domain", 1, true) ~= nil, "library asin(" .. tostring(bad) .. "): " .. tostring(err2))

    local ok3, err3 = pcall(function() return math.acos(bad) end)
    assert(not ok3 and string.find(err3, "math.acos domain", 1, true) ~= nil, "fastcall acos(" .. tostring(bad) .. "): " .. tostring(err3))
    local ok4, err4 = pcall(f_acos, bad)
    assert(not ok4 and string.find(err4, "math.acos domain", 1, true) ~= nil, "library acos(" .. tostring(bad) .. "): " .. tostring(err4))
end

return "OK"
