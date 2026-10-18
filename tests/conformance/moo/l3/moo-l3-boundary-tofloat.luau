-- moo: the out-of-VM boundary to float (docs/DESIGN.md, "Boundary"). The encode itself is host-side
-- (moo_tofloat / moo_numtofloatbits, tested in tests/moo/moo-l3-boundary-tofloat.test.cpp); what a script
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
assert(max + step == max, tostring(max + step)) -- saturates at the top
assert(min - step == min, tostring(min - step)) -- saturates at the bottom
assert(max - 1 < max)

return "OK"
