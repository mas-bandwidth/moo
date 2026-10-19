-- This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
-- moo: layer 3 removes script-visible vector and integer libraries from luaL_openlibs.
-- moo-l3-libs-out: luaL_openlibs opens no vector and no integer library (docs/DESIGN.md, "Layer 3 as built").
print('testing no vector and no integer library')

assert(vector == nil)
assert(integer == nil)

local ok, err = pcall(function() return vector.create(1, 2, 3) end)
assert(not ok)
assert(string.find(err, "attempt to index nil with 'create'", 1, true))

for _, v in ipairs({0, -1, 0.5, 1e10, "x", {}, function() end, true}) do
	assert(typeof(v) ~= "vector")
end

return 'OK'
