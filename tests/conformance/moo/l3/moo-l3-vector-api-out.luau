-- This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
-- moo: layer 3 removes the float vector from the C API (docs/DESIGN.md "vec3, quat and transform").
-- The C++ case pins that luaL_openlibs and luaL_sandbox leave no vector-valued field; a script can still
-- pin that no script-visible path makes one.
assert(rawget(_G, "vector") == nil, "vector is not a global")

for name, value in pairs(_G) do
	assert(typeof(value) ~= "vector", "global " .. tostring(name) .. " is a vector")
end

local ok = pcall(function() return vector.create(1, 2, 3) end)
assert(not ok, "no script can create a vector")

return "OK"
