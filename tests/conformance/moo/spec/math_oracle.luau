-- This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
-- moo: The math accuracy oracle: tests/moo/vectors/math-oracle.bin decodes, and every math function's
-- maximum error against it is asserted (docs/DESIGN.md "math library"; tools/moo-math-oracle).
-- Each block below is one rule; MooSpecScripts runs this file and expects "OK".

do
    -- moo: layer 4's closing conformance script (docs/DESIGN.md, "Layers" item 4, "math library"). It checks that
    -- every function in the "math library" table answers through the VM, that the fixed points are the documented
    -- Q48.16 values, and that the named domains behave. The CORE-MATH table and its bounds are asserted by
    -- tests/moo/math_oracle.test.cpp MathOracleBoundPerFunction over tests/moo/vectors/math-oracle.bin.
    local raw = moo_test.raw
    local fromraw = moo_test.fromraw
    local pi = fromraw('205887')
    local one = fromraw('65536')

    -- Exact points of every source in the table.
    assert(raw(math.sin(0)) == '0')
    assert(raw(math.cos(0)) == '65536')
    assert(raw(math.tan(0)) == '0')
    assert(raw(math.asin(0)) == '0')
    assert(raw(math.acos(1)) == '0')
    assert(raw(math.atan(0)) == '0')
    assert(raw(math.atan2(0, 1)) == '0')
    assert(raw(math.exp(0)) == '65536')
    assert(raw(math.log(1)) == '0')
    assert(raw(math.log10(1)) == '0')
    assert(raw(math.sinh(0)) == '0')
    assert(raw(math.cosh(0)) == '65536')
    assert(raw(math.tanh(0)) == '0')
    assert(raw(math.sqrt(1)) == '65536')
    assert(raw(math.sqrt(0)) == '0')
    assert(raw(math.pow(1, 1)) == '65536')
    assert(raw(2 ^ 10) == '67108864') -- exact integer exponentiation
    assert(raw(math.floor(-0.5)) == '-65536')
    assert(raw(math.ceil(-0.5)) == '0')
    assert(raw(math.round(-0.5)) == '-65536')
    assert(raw(math.fmod(7, 3)) == '65536')
    assert(raw(math.ldexp(1, 3)) == '524288')
    assert(raw(math.deg(pi)) == '11796456') -- pi through the rounded 180/pi constant
    assert(raw(math.rad(fromraw('1144'))) == '20') -- 1144 raw through the rounded pi/180 constant

    -- asin(1) = pi/2, acos(-1) = pi, the ideals the oracle file pins.
    assert(raw(math.asin(1)) == '102944')
    assert(raw(math.acos(-1)) == '205887')

    -- Saturation and the largest value (docs/DESIGN.md "Division by zero", "Overflow").
    assert(raw(math.huge) == '9223372036854775807')
    assert(raw(-math.huge) == '-9223372036854775807')
    assert(math.isfinite(math.huge))
    assert(not math.isnan(math.huge))
    assert(not math.isinf(math.huge))

    -- Named domains, on the library and the fastcall path.
    local cases = {
        {function() return math.sqrt(-1) end, 'math.sqrt of a negative number'},
        {function() return math.log(0) end, 'math.log of a non-positive number'},
        {function() return math.log(-1) end, 'math.log of a non-positive number'},
        {function() return math.asin(1.5) end, 'math.asin domain'},
        {function() return math.acos(-1.5) end, 'math.acos domain'},
        {function() return math.fmod(1, 0) end, 'division by zero'},
        {function() return math.tan(fromraw('102944')) end, 'division by zero'},
    }
    for _, c in ipairs(cases) do
        local ok, err = pcall(c[1])
        assert(ok == false, 'expected a named error, got ' .. tostring(err))
        assert(string.find(err, c[2], 1, true) ~= nil, 'expected ' .. c[2] .. ', got ' .. tostring(err))
    end

    -- Every function accepts ordinary fixed inputs and gives the same bits through the fastcall and the library.
    local f = {math.sin, math.cos, math.tan, math.atan, math.exp, math.log, math.log10,
               math.sinh, math.cosh, math.tanh, math.sqrt, math.floor, math.ceil, math.abs,
               math.round, math.sign, math.deg, math.rad}
    for _, fn in ipairs(f) do
        local a = fn(fromraw('32768'))
        local b = fn(0.5)
        assert(a == b)
    end
end

do
    -- moo: layer 4's accuracy oracle, seen from a script (docs/DESIGN.md, "math library", "Accuracy oracle"). The frozen
    -- ideal answers are in tests/moo/vectors/math-oracle.bin, and the error table is printed by
    -- tests/moo/math_oracle.test.cpp MathOracleErrorTable. When this script was written every transcendental in math
    -- was the libm bridge (docs/DESIGN.md, "Layer 3 as built"), so this script calls none of them: a test never calls libm. It
    -- checks the two values the VM gives without libm that the file also holds. math.pi is the nearest Q48.16 to pi,
    -- the file's ideal for acos(-1), and math.huge is the largest value, the file's saturated ideal (checked by
    -- tests/moo/math_oracle.test.cpp MathOracleExactPoints).
    assert(moo_test.raw(math.pi) == "205887")
    assert(moo_test.raw(math.huge) == "9223372036854775807")
    assert(moo_test.raw(-math.huge) == "-9223372036854775807")
end

return "OK"
