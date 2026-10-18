-- moo: the compiler's number constants are the raw Q48.16 value (docs/layer3/moo-l3-compiler-raw-constants.md;
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

return "OK"
