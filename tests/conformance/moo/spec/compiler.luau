-- This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
-- moo: The compiler: constant folding and Sccp fold on the raw Q48.16 value with moo_number.h, and give
-- the VM's result bit for bit (docs/DESIGN.md "Layer 3 as built").
-- Each block below is one rule; MooSpecScripts runs this file and expects "OK".

do
    -- moo: the compiler's number constants are the raw Q48.16 value (docs/history/layer3/moo-l3-compiler-raw-constants.md;
    -- docs/DESIGN.md "Layer 3 as built", "The compiler"): a folded constant is the exact raw value, with no double in
    -- the path and no 2^53 limit. 2^37 + 2^37 folds to raw 2^54, and folding an expression and running it give the
    -- same bits at every magnitude.
    local compile = moo_test.compile
    local load = moo_test.load
    local raw = moo_test.raw

    -- the exact decimal literal of a raw value (the parser reads it exactly)
    local function exact(r)
        local m = r < 0 and -r or r
        local sign = r < 0 and "-" or ""
        return sign .. (m // 65536) .. string.format(".%016d", m % 65536)
    end

    -- 2^37 + 2^37 folds to raw 2^54 = 18014398509481984
    local fn, err = load(compile("return 137438953472 + 137438953472"))
    assert(fn, err)
    assert(raw(fn()) == "18014398509481984")

    -- folded equals runtime, bit for bit, over 256 seeded operand pairs
    math.randomseed(42)
    for i = 1, 256 do
        local a = math.random(-2^20, 2^20)
        local b = math.random(-2^20, 2^20)
        local la, lb = exact(a), exact(b)

        local ff, ferr = load(compile("return " .. la .. " + " .. lb))
        assert(ff, ferr)

        local rf, rerr = load(compile("local f = function(a, b) return a + b end return f(" .. la .. ", " .. lb .. ")"))
        assert(rf, rerr)

        assert(raw(ff()) == raw(rf()), "pair " .. i)
    end
end

do
    -- moo: the bytecode-graph constant propagation (Sccp) folds with moo_number.h, so a folded constant is the VM's
    -- result (docs/history/layer3/moo-l3-bytecode-graph-fixed.md; docs/DESIGN.md "Layer 3 as built"). The raw Q48.16 values
    -- below are what the VM computes; the compiler may fold them into constants, but the observable value is the same.
    local function check(expr, want)
        local got = expr()
        assert(moo_test.raw(got) == want, moo_test.raw(got) .. " want " .. want)
    end

    check(function() return 1.5 + 2.25 end, "245760")   -- 3.75 * 65536
    check(function() return 10 / 4 end, "163840")       -- 2.5 * 65536
    check(function() return 7 // 2 end, "196608")       -- 3 * 65536
    check(function() return 7 % 3 end, "65536")         -- 1 * 65536

    -- division by zero (and % by zero) are never folded: the VM raises the named error at run time
    local ok, err = pcall(function() return 10 / 0 end)
    assert(not ok, "10/0 should error")
    assert(err and err:find("division by zero") ~= nil, tostring(err))

    local ok2, err2 = pcall(function() return 10 % 0 end)
    assert(not ok2, "10%0 should error")
    assert(err2 and err2:find("division by zero") ~= nil, tostring(err2))
end

return "OK"
