-- This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
-- moo: The boundary: a host double or float converts to Q48.16 by the one rule (nearest, ties to even,
-- saturating, NaN to 0), and a number converts back the same way (docs/DESIGN.md "Boundary").
-- Each block below is one rule; MooSpecScripts runs this file and expects "OK".

do
    -- moo: the out-of-VM boundary to float (docs/DESIGN.md, "Boundary"). The encode itself is host-side
    -- (moo_tofloat / moo_numtofloatbits, tested in tests/moo/boundary.test.cpp); what a script
    -- can observe is the number model it builds on: Q48.16 fixed point, whose smallest step is 2^-16, so
    -- 1 + 2^-16 is the first value after 1 and 2^-16 is the least nonzero value.
    local step = moo_test.fromraw("1")
    assert(moo_test.raw(step) == "1", moo_test.raw(step))
    assert(1 + step > 1)
    assert(1 + step - 1 == step, tostring(1 + step - 1))

    local one = moo_test.fromraw("65536")
    assert(moo_test.raw(one) == "65536", moo_test.raw(one))
    assert(one == 1)
    assert(one + step == 1 + step)

    local max = moo_test.fromraw("9223372036854775807") -- INT64_MAX, 2^47 - 2^-16
    local min = moo_test.fromraw("-9223372036854775807")
    -- moo: release saturates at the top and the bottom; a checked build raises the named error instead
    -- (docs/DESIGN.md, "Overflow"; tests/conformance/moo/spec/number.luau)
    local function saturates(fn, want, name)
        local ok, result = pcall(fn)
        local suffix = "number overflow in '" .. name .. "'"
        assert((ok and result == want) or (not ok and string.sub(result, -#suffix) == suffix), tostring(result))
    end
    saturates(function() return max + step end, max, "+")
    saturates(function() return min - step end, min, "-")
    assert(max - 1 < max)
end

do
    -- moo: the temporary double bridge is deleted (docs/history/layer4/moo-l4-bridge-removed.md; docs/DESIGN.md
    -- "Layer 3 as built", TEMPORARY bridges). Every math result below is the fixed one, not a libm double
    -- rounded on the way back; tostring and tonumber use the exact formatter and parser, and the heap dump
    -- formats a numeric key with moo_format.h rather than printf.
    assert(moo_test.raw(math.sqrt(4)) == "131072", moo_test.raw(math.sqrt(4)))
    assert(moo_test.raw(math.sqrt(2)) == "92681", moo_test.raw(math.sqrt(2))) -- fixSqrt, toward zero
    assert(math.sin(0) == 0)
    assert(math.cos(0) == 1)
    assert(math.atan2(0, 1) == 0)
    assert(math.exp(0) == 1)
    assert(math.log(1) == 0)
    assert(math.pow(2, 10) == 1024)
    assert(2 ^ 10 == 1024)
    assert(2 ^ 0.5 > 1.4 and 2 ^ 0.5 < 1.42) -- fixPow, not the libm bridge
    assert(tostring(0.1 + 0.2) == "0.3")
    assert(tostring(3) == "3")
    assert(tonumber("0.1") == 0.1)
    assert(tonumber(tostring(1.5)) == 1.5)
    assert(math.abs(-1.5) == 1.5)
    assert(math.floor(-1.5) == -2)
    assert(math.ceil(-1.5) == -1)
    assert(math.round(2.5) == 3)
    assert(moo_test.raw(math.max(1, 2) + 0.5) == "163840", moo_test.raw(math.max(1, 2) + 0.5))

    -- the exact formatter prints the shortest decimal that reads back to the same raw value
    assert(tostring(moo_test.fromraw("6554")) == "0.1")
    assert(tostring(moo_test.fromraw("-98304")) == "-1.5")
end

return "OK"
