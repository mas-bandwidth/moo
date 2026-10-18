-- This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
-- moo: layer 3 removes the script-visible vector library; the compiler and VM vector halves remain separate.
print('testing the float vector library is out')

-- layer 3b: the float32 vector library, type and bytecode constant are each refused by name (docs/DESIGN.md)
assert(vector == nil)

-- vector.create raises the index error on nil
local ok, err = pcall(function() return vector.create(1, 2, 3) end)
assert(not ok)
assert(string.find(err, "attempt to index nil with 'create'", 1, true))

-- typeof of every value the library used to make is never 'vector'
for _, v in ipairs({0, -1, 0.5, 1e10, "x", {}, function() end, true}) do
	assert(typeof(v) ~= "vector")
end

return 'OK'
