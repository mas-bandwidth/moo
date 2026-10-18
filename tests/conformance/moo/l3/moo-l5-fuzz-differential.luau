-- moo: the layer 5 differential fuzz target's rules, from inside the VM (docs/DESIGN.md "Layers", layer 5: fuzzing;
-- docs/layer5/moo-l5-fuzz-differential.md). The libFuzzer runs are functional and never run here; this script pins the
-- arithmetic, the round trip (moo_numtostr + moo_numparse) and the exact formatter on concrete raw values.
local raws = {
    "0", "1", "-1", "3", "-3", "32768", "-32768", "65536", "-65536", "6554", "-6554", "98304", "-98304",
    "9223372036854775807", "-9223372036854775807",
}

for _, s in ipairs(raws) do
    local x = moo_test.fromraw(s)
    assert(tonumber(tostring(x)) == x, "round-trip of raw " .. s)
    assert(tonumber(string.format("%.16f", x)) == x, "%.16f round-trip of raw " .. s)
end

-- fixes's fixMul rounds half up: 3 raw * 0.5 = 1.5 raw -> 2 raw (DESIGN.md "Semantics")
assert(moo_test.raw(moo_test.fromraw("3") * moo_test.fromraw("32768")) == "2")
-- exact add and sub, saturating at the domain edge (DESIGN.md "Overflow")
assert(moo_test.raw(moo_test.fromraw("9223372036854775807") + moo_test.fromraw("65536")) == "9223372036854775807")
assert(moo_test.raw(moo_test.fromraw("-9223372036854775807") - moo_test.fromraw("65536")) == "-9223372036854775807")
-- floor division and remainder have the sign of the divisor (DESIGN.md "Semantics")
assert(moo_test.raw(moo_test.fromraw("-98304") // moo_test.fromraw("65536")) == "-131072")
assert(moo_test.raw(moo_test.fromraw("-98304") % moo_test.fromraw("65536")) == "32768")
-- division by zero is the named error (DESIGN.md "Division by zero")
local one = moo_test.fromraw("65536")
local zero = moo_test.fromraw("0")
for i, f in ipairs({
    function() return one / zero end,
    function() return one // zero end,
    function() return one % zero end,
}) do
    local ok, err = pcall(f)
    assert(not ok and string.find(tostring(err), "division by zero", 1, true), "operator " .. tostring(i))
end

return "OK"
