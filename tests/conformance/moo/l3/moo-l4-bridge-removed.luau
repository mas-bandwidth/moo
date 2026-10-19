-- moo: the temporary double bridge is deleted (docs/layer4/moo-l4-bridge-removed.md; docs/DESIGN.md
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

return "OK"
