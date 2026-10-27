-- This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
-- moo: The math library on fixed: exact operations, exp and log, trig and inverse trig, sqrt and noise,
-- with the library and fastcall paths giving the same bits (docs/DESIGN.md "math library").
-- Each block below is one rule; MooSpecScripts runs this file and expects "OK".

do
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
end

do
    -- moo: DESIGN.md "math library": exp/sinh/cosh/tanh use fixExp2; log/log10/log(x, base) use fixLog2
    -- with the fixed scaling compositions. A direct call exercises the bytecode fastcall; an alias exercises the library.
    local exp_lib = math.exp
    local sinh_lib = math.sinh
    local cosh_lib = math.cosh
    local tanh_lib = math.tanh
    local log_lib = math.log
    local log_base_lib = math.log
    local log10_lib = math.log10

    assert(math.exp(0) == 1)
    assert(math.exp(100) == math.huge)
    assert(math.exp(-100) == 0)
    assert(math.sinh(0) == 0)
    assert(math.cosh(0) == 1)
    assert(math.tanh(0) == 0)
    assert(math.sinh(100) == math.huge)
    assert(math.sinh(-100) == -math.huge)
    assert(math.cosh(100) == math.huge)
    assert(math.tanh(100) == 1)
    assert(math.tanh(-100) == -1)
    assert(math.log(1) == 0)
    assert(math.log10(1) == 0)
    assert(math.log(8, 2) == 3)

    for _, x in ipairs({-40, -1, 0, 1, 10, 40}) do
        assert(exp_lib(x) == math.exp(x), "exp fastcall vs library")
        assert(sinh_lib(x) == math.sinh(x), "sinh fastcall vs library")
        assert(cosh_lib(x) == math.cosh(x), "cosh fastcall vs library")
        assert(tanh_lib(x) == math.tanh(x), "tanh fastcall vs library")
    end

    for _, x in ipairs({1, 2, 8, 10, 100}) do
        assert(log_lib(x) == math.log(x), "log fastcall vs library")
        assert(log10_lib(x) == math.log10(x), "log10 fastcall vs library")
        assert(log_base_lib(x, 2) == math.log(x, 2), "log(x, 2) fastcall vs library")
        assert(log_base_lib(x, 3) == math.log(x, 3), "log(x, 3) fastcall vs library")
    end

    local function domainError(call)
        local ok, err = pcall(call)
        return not ok and string.find(err, "math.log of a non-positive number", 1, true) ~= nil
    end
    assert(domainError(function() return math.log(0) end))
    assert(domainError(function() return log_lib(-1) end))
    assert(domainError(function() return math.log(1, 0) end))
    assert(domainError(function() return math.log(1, -1) end))
    assert(domainError(function() return math.log10(0) end))
    assert(domainError(function() return log10_lib(-1) end))

    local ok, err = pcall(function() return math.log(1, 1) end)
    assert(not ok and string.find(err, "division by zero", 1, true) ~= nil, err)
    ok, err = pcall(function() return log_lib(1, 1) end)
    assert(not ok and string.find(err, "division by zero", 1, true) ~= nil, err)
end

do
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
end

do
    -- moo: DESIGN.md "math library": math.sin, math.cos and math.tan on fixed. sin and cos come from
    -- fixComputeCosSin (fixSin/fixCos) and tan is sin/cos with a zero cosine raising "division by zero" --
    -- the library and the fastcall share one definition, so they give the same bits. The direct calls below
    -- take the bytecode fastcall; the local aliases go through the library.
    local f_sin = math.sin
    local f_cos = math.cos
    local f_tan = math.tan

    local pi = moo_test.fromraw('205887')     -- round(pi * 2^16)
    local halfpi = moo_test.fromraw('102944') -- round(pi/2 * 2^16)

    -- sin/cos at the special angles agree between the fastcall and the library path.
    for _, a in ipairs{0, halfpi, 2 * halfpi, 3 * halfpi, -halfpi, -2 * halfpi, -3 * halfpi} do
        assert(f_sin(a) == math.sin(a), "sin fastcall vs library")
        assert(f_cos(a) == math.cos(a), "cos fastcall vs library")
    end

    -- sin(0) = 0, cos(0) = 1, sin(pi/2) = 1, cos(pi/2) = 0, sin(pi) = 0, cos(pi) = -1.
    assert(math.sin(0) == 0)
    assert(math.cos(0) == 1)
    assert(math.sin(halfpi) == 1)
    assert(math.cos(halfpi) == 0)
    assert(math.sin(pi) == 0)
    assert(math.cos(pi) == -1)

    -- tan = sin / cos; a zero cosine is "division by zero", on the fastcall and the library path.
    local ok, err = pcall(math.tan, halfpi)
    assert(ok == false and err == "division by zero", err)
    ok, err = pcall(f_tan, -halfpi)
    assert(ok == false and err == "division by zero", err)
    ok, err = pcall(f_tan, halfpi)
    assert(ok == false and err == "division by zero", err)

    -- tan and its fastcall agree where the cosine is not zero.
    for _, a in ipairs{0, pi, 2 * pi, -pi, 1} do
        assert(f_tan(a) == math.tan(a), "tan fastcall vs library")
    end
    assert(math.tan(0) == 0)
    assert(math.tan(pi) == 0)
end

do
    -- moo: layer 4's sqrt, through the VM. math.sqrt is fixed's fixSqrt: exact, rounded toward zero, and a
    -- negative input is the named error (docs/DESIGN.md, "math library": sqrt from fixSqrt). The exhaustive
    -- pool is in tests/moo/math.test.cpp; this file pins the rule on concrete raw values
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
end

do
    -- moo: DESIGN.md "math library": math.noise is Luau's Perlin noise on fixed Q48.16.
    local raw = moo_test.raw
    local noise = math.noise

    for x = -2, 2 do
        for y = -2, 2 do
            for z = -2, 2 do
                assert(noise(x, y, z) == 0, "noise is zero at lattice points")
            end
        end
    end

    local sample = noise(0.25, 0.5, 0.75)
    assert(raw(math.noise(0.25, 0.5, 0.75)) == raw(noise(0.25, 0.5, 0.75)), "fastcall and library results match")
    assert(sample >= -1 and sample <= 1, "noise stays in [-1, 1]")
    assert(raw(noise(0.25, 0.5, 0.75)) == raw(noise(256.25, 0.5, 0.75)), "noise repeats every 256 cells")
    assert(noise(0.25) == noise(0.25, 0, 0), "missing coordinates default to zero")
    assert(math.noise(0.25) == noise(0.25), "fastcall supplies default coordinates")
    assert(noise(0.25, 0.5) == noise(0.25, 0.5, 0), "missing z defaults to zero")
    assert(noise(-0.25, -0.5, -0.75) == noise(255.75, 255.5, 255.25), "negative coordinates wrap by the period")
end

return "OK"
