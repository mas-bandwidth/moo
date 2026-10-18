-- moo: DESIGN.md "math library": fmod, modf, frexp, ldexp, deg, rad, lerp, pi and huge are exact
-- integer operations on the raw Q48.16 value (no libm / double bridge). Direct math.* calls take
-- the bytecode fastcall; local aliases go through the C library entry points.

local f_fmod = math.fmod
local f_modf = math.modf
local f_frexp = math.frexp
local f_ldexp = math.ldexp
local f_deg = math.deg
local f_rad = math.rad
local f_lerp = math.lerp

local raw = moo_test.raw
local fromraw = moo_test.fromraw

-- 1. math.pi and math.huge constants
assert(raw(math.pi) == "205887", "math.pi raw should be 205887")
assert(raw(math.huge) == "9223372036854775807", "math.huge raw should be INT64_MAX")

-- 2. math.fmod: raw truncated remainder; b == 0 raises "division by zero" on both fastcall and library
local ok1, err1 = pcall(function() return math.fmod(1, 0) end)
assert(not ok1 and string.find(err1, "division by zero", 1, true) ~= nil, "fastcall fmod(1, 0): " .. tostring(err1))
local ok2, err2 = pcall(f_fmod, 1, 0)
assert(not ok2 and string.find(err2, "division by zero", 1, true) ~= nil, "library fmod(1, 0): " .. tostring(err2))

-- High-magnitude Q48.16 value with 1 raw fraction bit (2^40 + 2^-16 = 72057594037927937 raw),
-- which loses the low bit if converted through IEEE-754 double (53-bit significand).
local bigPlusEps = fromraw("72057594037927937")
assert(raw(math.fmod(bigPlusEps, 1)) == "1", "fastcall fmod(2^40 + 2^-16, 1) must keep low bit")
assert(raw(f_fmod(bigPlusEps, 1)) == "1", "library fmod(2^40 + 2^-16, 1) must keep low bit")
assert(raw(math.fmod(-bigPlusEps, 1)) == "-1", "fastcall fmod(-(2^40 + 2^-16), 1) must keep low bit")
assert(raw(f_fmod(-bigPlusEps, 1)) == "-1", "library fmod(-(2^40 + 2^-16), 1) must keep low bit")

-- 3. math.modf: raw integer and fraction parts across the full 63-bit Q48.16 range
local ip1, fp1 = math.modf(bigPlusEps)
local ip2, fp2 = f_modf(bigPlusEps)
assert(raw(ip1) == "72057594037927936" and raw(fp1) == "1", "fastcall modf(2^40 + 2^-16)")
assert(raw(ip2) == "72057594037927936" and raw(fp2) == "1", "library modf(2^40 + 2^-16)")

local nip1, nfp1 = math.modf(-bigPlusEps)
local nip2, nfp2 = f_modf(-bigPlusEps)
assert(raw(nip1) == "-72057594037927936" and raw(nfp1) == "-1", "fastcall modf(-(2^40 + 2^-16))")
assert(raw(nip2) == "-72057594037927936" and raw(nfp2) == "-1", "library modf(-(2^40 + 2^-16))")

-- 4. math.frexp: exact binary mantissa in [0.5, 1) (raw [32768, 65535]) and exponent
local m0, e0 = math.frexp(0)
assert(raw(m0) == "0" and raw(e0) == "0")
local m0L, e0L = f_frexp(0)
assert(raw(m0L) == "0" and raw(e0L) == "0")

-- 2 - 2^-16 (raw 131071 = 0x1ffff) has msb = 16 -> e = 1 and mantissa raw 65535 (0xffff < 1.0)
local nearTwo = fromraw("131071")
local mf, ef = math.frexp(nearTwo)
local ml, el = f_frexp(nearTwo)
assert(raw(mf) == "65535" and ef == 1, "fastcall frexp(2 - 2^-16) must stay in [0.5, 1)")
assert(raw(ml) == "65535" and el == 1, "library frexp(2 - 2^-16) must stay in [0.5, 1)")

-- 5. math.ldexp: exact shift, saturating, non-integer exponent raises "number has no integer representation"
assert(raw(math.ldexp(fromraw("1"), 62)) == "4611686018427387904")
assert(raw(math.ldexp(fromraw("1"), 63)) == "9223372036854775807")
assert(raw(math.ldexp(fromraw("-1"), 63)) == "-9223372036854775807")
assert(raw(math.ldexp(fromraw("-1"), -1)) == "0")
assert(raw(f_ldexp(fromraw("1"), 63)) == "9223372036854775807")
assert(raw(f_ldexp(fromraw("-1"), 63)) == "-9223372036854775807")
assert(raw(f_ldexp(fromraw("-1"), -1)) == "0")

local ok3, err3 = pcall(function() return math.ldexp(1, 1.5) end)
assert(not ok3 and string.find(err3, "number has no integer representation", 1, true) ~= nil, tostring(err3))
local ok4, err4 = pcall(f_ldexp, 1, 1.5)
assert(not ok4 and string.find(err4, "number has no integer representation", 1, true) ~= nil, tostring(err4))

-- 6. math.deg and math.rad: one fixMul by nearest Q48.16 of 180/pi (3754936) and pi/180 (1144)
assert(raw(math.deg(1)) == "3754936" and raw(f_deg(1)) == "3754936")
assert(raw(math.rad(1)) == "1144" and raw(f_rad(1)) == "1144")
assert(raw(math.rad(3)) == "3432" and raw(f_rad(3)) == "3432")

-- 7. math.lerp: t == 1 ? b : a + (b - a) * t on moo operators
assert(raw(math.lerp(fromraw("1"), fromraw("4"), fromraw("32768"))) == "3")
assert(raw(f_lerp(fromraw("1"), fromraw("4"), fromraw("32768"))) == "3")
assert(math.lerp(bigPlusEps, -bigPlusEps, 1) == -bigPlusEps)
assert(f_lerp(bigPlusEps, -bigPlusEps, 1) == -bigPlusEps)

return "OK"
