-- This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
-- moo: samples the runtime side of the upstream removals that docs/PATCHES.md records. The
-- inventory itself is checked by tools/moo-check-patches.sh in the push gate; this script pins
-- what a few of those rows are visible as from inside the VM.
-- moo-l3-patch-inventory: os (VM/src/loslib.cpp), the vector and integer libraries, and the raw
-- Q48.16 number are the observables the inventory's VM rows name (docs/DESIGN.md "Layer 3 as
-- built", DETERMINISM.md, 9).

assert(rawget(_G, "os") == nil, "os must not be a global")
assert(rawget(_G, "vector") == nil, "vector must not be a global")
assert(rawget(_G, "integer") == nil, "integer must not be a global")

assert(moo_test.raw(0.5) == "32768", moo_test.raw(0.5))
assert(moo_test.fromraw("32768") == 0.5)

-- The loader accepts only moo bytecode and refuses a Luau blob by version (VM/src/lvmload.cpp).
local fn, err = moo_test.load(string.char(0), "=luau")
assert(fn == nil, "a non-moo bytecode blob loaded")
assert(type(err) == "string", tostring(err))

return "OK"
