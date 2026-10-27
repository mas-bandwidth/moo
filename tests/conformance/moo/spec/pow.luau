-- This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
-- moo: pow and ^: moo_pow_int is correctly rounded for an integer exponent, the compiler folds it, the VM
-- and math.pow use it, and a fractional exponent uses fixPow (docs/DESIGN.md "math library").
-- Each block below is one rule; MooSpecScripts runs this file and expects "OK".

-- a checked build (MOO_OVERFLOW_ERRORS) raises where release saturates; + says which build this is
local checked = not pcall(function(a, b) return a + b end, moo_test.fromraw("9223372036854775807"), moo_test.fromraw("1"))

-- the message without its "chunk:line: " prefix
local function message(err)
    return (string.gsub(tostring(err), "^[^:]*:%d+: ", ""))
end

do
    -- moo: integer-exponent pow in the math library (docs/DESIGN.md "math library", "pow and ^"): math.pow
    -- and its fastcall call moo_pow_int, so an integer-valued exponent is exact and rounded once, ties to
    -- even, saturating in release; 0 ^ negative is "division by zero"; a fractional exponent is
    -- fixPow (docs/history/layer4/moo-l4-pow-integer.md).

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

    -- Release saturates: 2 ^ 47 and (-2) ^ 47 land on the edges. A checked build raises.
    if checked then
        for _, baseRaw in ipairs({"131072", "-131072"}) do
            local ok, err = pcall(rawPow, moo_test.fromraw(baseRaw), 47 * one)
            assert(not ok and message(err) == "number overflow in '^'", baseRaw .. " ^ 47: " .. tostring(err))
        end
    else
        assert(rawPow(moo_test.fromraw("131072"), 47 * one) == moo_test.raw(math.huge), "2 ^ 47 saturates")
        assert(rawPow(moo_test.fromraw("-131072"), 47 * one) == moo_test.raw(-math.huge), "(-2) ^ 47 saturates")
    end

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
    -- moo: fixPow's value is pinned in pow.luau; 27 (1769472) was the libm bridge's.
    assert(rawPow(moo_test.fromraw("589824"), 1.5) == "1769466", "9 ^ 1.5")
end

do
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

    -- A negative base with a fractional exponent is the named error, in math.pow and the opcode alike.
    local ok, err = pcall(function() return math.pow(-1, fromraw('32768')) end)
    assert(not ok and tostring(err):find('pow of a negative number to a fractional power', 1, true))
    local ok2, err2 = pcall(function() return math.pow(-4, fromraw('-98304')) end)
    assert(not ok2 and tostring(err2):find('pow of a negative number to a fractional power', 1, true))
    local ok3, err3 = pcall(function() return fromraw('-65536') ^ fromraw('32768') end)
    assert(not ok3 and message(err3) == 'pow of a negative number to a fractional power', tostring(err3))

    -- An integer exponent still takes moo_pow_int, no error.
    assert(raw(math.pow(fromraw('-131072'), fromraw('196608'))) == '-524288')
    assert(raw(fromraw('-131072') ^ 3) == '-524288')
end

do
    -- moo: DESIGN.md "math library", "pow and ^": ^ with an integer-valued exponent is moo_pow_int, exact and
    -- rounded once, ties to even, saturating; 0 ^ negative is "division by zero". These are concrete raw values
    -- from docs/history/layer4/moo-l4-pow-integer.md, run through the VM.
    local fromraw = moo_test.fromraw
    local raw = moo_test.raw

    -- x ^ 0 = 1 for every x, 0 included; 0 ^ positive = 0.
    assert(raw(fromraw('0') ^ 0) == '65536')
    assert(raw(fromraw('1234567') ^ 0) == '65536')
    assert(raw(fromraw('0') ^ 2) == '0')
    assert(raw(fromraw('0') ^ 46) == '0')

    -- Exact integer powers: 2 ^ 12 and 2 ^ 46 are in range, 2 ^ 47 saturates in release and raises when checked.
    assert(raw(2 ^ 12) == '268435456')
    assert(raw(2 ^ 46) == '4611686018427387904')
    local ok, result = pcall(function() return raw(2 ^ 47) end)
    if checked then
        assert(not ok and message(result) == "number overflow in '^'", tostring(result))
    else
        assert(ok and result == '9223372036854775807', tostring(result))
    end

    -- A negative exponent rounds once, half to even: 6 ^ -1 is 10923 where 1 / 6 truncates to 10922.
    assert(raw(fromraw('393216') ^ -1) == '10923')
    assert(raw(fromraw('65536') / fromraw('393216')) == '10922')
    assert(raw(fromraw('196608') ^ -2) == '7282')

    -- The exact tie 1.5 ^ 17 = 3 ^ 17 / 2 rounds to even.
    assert(raw(fromraw('98304') ^ 17) == '64570082')

    -- A negative base keeps its sign for an odd exponent.
    assert(raw(fromraw('-131072') ^ 3) == '-524288')
    assert(raw(fromraw('-131072') ^ 2) == '262144')

    -- 0 ^ negative is "division by zero", through POWK, POW and a coerced operand, as math.pow.
    for _, call in ipairs({
        function() return fromraw('0') ^ -1 end,
        function() return fromraw('0') ^ fromraw('-65536') end,
        function() return '0' ^ fromraw('-65536') end,
    }) do
        local ok0, err0 = pcall(call)
        assert(not ok0 and message(err0) == 'division by zero', tostring(err0))
    end
end

do
    -- moo: the ^ operator is math.pow (docs/DESIGN.md "pow and ^"): for each case the operator with registers, with
    -- a constant exponent, with both operands constant (folded or left to run time), math.pow and its fastcall give
    -- the same value or the same error, at -O0, -O1 and -O2. A case gives its release value, or its error.
    local maxRaw = "9223372036854775807"
    local overflow = "number overflow in '^'"
    local cases = {
        -- base, exponent, release raw value or error, error only when checked
        {"0", "-1", "division by zero"},
        {"0", "-3", "division by zero"},
        {"(-4)", "0.5", "pow of a negative number to a fractional power"},
        {"(-1)", "-1.5", "pow of a negative number to a fractional power"},
        {"2", "47", maxRaw, overflow},
        {"(-2)", "47", "-" .. maxRaw, overflow},
        {"10", "100000", maxRaw, overflow},
        {"2", "47.5", maxRaw, overflow},
        {"2", "46", "4611686018427387904"},
        {"6", "-1", "10923"},
        {"0", "0", "65536"},
        {"0", "0.5", "0"},
        {"0", "-0.5", "division by zero"},
        {"0", "-1.5", "division by zero"},
        {"4", "0.5", "131072"},
        {"(-2)", "3", "-524288"},
    }

    local function run(source, level)
        local f, loadErr = moo_test.load(moo_test.compile(source, level), "=pow")
        assert(f, loadErr)
        local ok, result = pcall(f)
        if ok then
            return result
        end
        return "error: " .. message(result)
    end

    for _, c in ipairs(cases) do
        local base, exponent, release, checkedError = c[1], c[2], c[3], c[4]
        local want = release
        if checked and checkedError then
            want = checkedError
        end
        if not string.find(want, "^%-?%d+$") then
            want = "error: " .. want
        end

        local sources = {
            "local t = {" .. base .. ", " .. exponent .. "} return moo_test.raw(t[1] ^ t[2])",
            "local t = {" .. base .. "} return moo_test.raw(t[1] ^ " .. exponent .. ")",
            "return moo_test.raw(" .. base .. " ^ " .. exponent .. ")",
            "local t = {" .. base .. ", " .. exponent .. "} return moo_test.raw(math.pow(t[1], t[2]))",
            "local p = rawget(math, 'pow') local t = {" .. base .. ", " .. exponent .. "} return moo_test.raw(p(t[1], t[2]))",
        }
        for level = 0, 2 do
            for _, source in ipairs(sources) do
                local got = run(source, level)
                assert(got == want, "-O" .. level .. " " .. source .. ": " .. got .. ", want " .. want)
            end
        end
    end
end

return "OK"
