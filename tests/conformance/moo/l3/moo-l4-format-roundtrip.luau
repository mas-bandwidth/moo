-- moo: layer 4's formatting check, through the VM. Every formatted value round-trips and string.format
-- prints the exact expansion, rounding half to even (docs/DESIGN.md, "Layer 3 as built" "Formatting and
-- tonumber are exact", and "Number formatting"). The exhaustive pool is in tests/moo/moo-l4-format-roundtrip.test.cpp;
-- this file pins the rules on concrete raw values (moo_test.fromraw builds a number from its raw Q48.16 value).
local raws = {
    "0", "1", "-1", "65536", "-65536", "6554", "-6554", "21845", "98304", "163840", "229376",
    "9223372036854775807", "-9223372036854775807",
}

for _, s in ipairs(raws) do
    local x = moo_test.fromraw(s)
    assert(tonumber(tostring(x)) == x, "round-trip of raw " .. s)
end

assert(moo_test.fromraw("6554") == 0.1)
assert(tostring(moo_test.fromraw("6554")) == "0.1")
assert(tostring(moo_test.fromraw("65536")) == "1")
assert(tostring(moo_test.fromraw("21845")) == "0.33333")
assert(tostring(moo_test.fromraw("-98304")) == "-1.5")
assert(tostring(moo_test.fromraw("9223372036854775807")) == "140737488355327.99998")

-- %.16f is the exact expansion: raw * 10^16 / 2^16
assert(string.format("%.16f", moo_test.fromraw("6554")) == "0.1000061035156250")
assert(string.format("%.16f", moo_test.fromraw("-65536")) == "-1.0000000000000000")
assert(string.format("%.16f", moo_test.fromraw("9223372036854775807")) == "140737488355327.9999847412109375")

-- %e / %g round half to even on the exact value: the raw value sits exactly at the halfway point
assert(string.format("%.0e", moo_test.fromraw("98304")) == "2e+00")   -- 1.5 -> 2 (even)
assert(string.format("%.0e", moo_test.fromraw("163840")) == "2e+00")  -- 2.5 -> 2 (even)
assert(string.format("%.0e", moo_test.fromraw("196608")) == "3e+00")  -- 3.0
assert(string.format("%.0e", moo_test.fromraw("229376")) == "4e+00")  -- 3.5 -> 4 (even)
assert(string.format("%.1e", moo_test.fromraw("98304")) == "1.5e+00")
assert(string.format("%.1g", moo_test.fromraw("98304")) == "2")
assert(string.format("%.2g", moo_test.fromraw("98304")) == "1.5")
assert(string.format("%.0g", moo_test.fromraw("98304")) == "2")
assert(string.format("%.2g", moo_test.fromraw("131072")) == "2")

return "OK"
