-- moo: layer 3b vec3 as a table key (moo-v2-l3b-vec3-object-key).
--
-- A vec3 cannot be built from script yet: the script constructor is card
-- moo-v2-l3b-vec3-ops (moo_pushvec3/moo_tovec3). Until that lands, the C++ test
-- (tests/moo/moo-v2-l3b-vec3-object-key.test.cpp) exercises equal vec3 keys, the frozen
-- component hash, the deterministic iteration order and GC survival through the internal
-- constructors. This script pins the contract that the moo_test helpers are present and
-- return "OK"; the vec3 exercising above is recorded in the card's note and the layer
-- closing card folds it in.
assert(type(moo_test) == "table")
assert(type(moo_test.raw) == "function")
assert(type(moo_test.fromraw) == "function")
assert(moo_test.raw(0) == "0")
assert(moo_test.fromraw("0") == 0)

return "OK"
