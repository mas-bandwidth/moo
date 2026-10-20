-- moo: layer 3b quat as a GC object (moo-v2-l3b-quat-object).
--
-- A quat cannot be built from script yet: the script constructor is card
-- moo-v2-l3b-quat (moo_pushquat/moo_toquat). Until that lands, the C++ test
-- (tests/moo/moo-v2-l3b-quat-object.test.cpp) exercises creation, value equality, the
-- table key, tostring and GC survival through the internal constructors. This script
-- pins the contract that the moo_test helpers are present and return "OK"; the quat
-- exercising above is recorded in the card's note and the layer closing card folds it in.
assert(type(moo_test) == "table")
assert(type(moo_test.raw) == "function")
assert(type(moo_test.fromraw) == "function")
assert(moo_test.raw(0) == "0")
assert(moo_test.fromraw("0") == 0)

return "OK"
