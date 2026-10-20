-- This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
print "testing builtin types"

local ignore =
{
	-- these are permanently ignored, as they are only exposed in tests
	"_G.limitedstack",
	"_G.RTTI",
	"_G.collectgarbage",

	-- what follows is a set of mismatches that hopefully eventually will go down to 0
	"_G.require", -- need to move to Roblox type defs

	-- moo: the analyzer's definitions drop vector (docs/DESIGN.md "Analyzer"), but the VM still
	-- exposes the vector library at this base, so ignore it until the VM removal lands.
	"_G.vector",

	-- moo: the analyzer's vec3 type is a later card (docs/layer3b/moo-v2-l3b-vec3-ops.md "Not done"),
	-- but the VM exposes the vec3 library now, so ignore it until that type lands.
	"_G.vec3",
}

function verify(real, rtti, path)
	if table.find(ignore, path) then
		return
	end

	if real and rtti then
		if type(real) == "table" then
			assert(type(rtti) == "table", path .. " is not a table in type information")

			local keys = {}

			for k, v in pairs(real) do
				keys[k] = 1
			end

			for k, v in pairs(rtti) do
				keys[k] = 1
			end

			for k, v in pairs(keys) do
				if k ~= "_G" then
					verify(real[k], rtti[k], path .. '.' .. k)
				end
			end
		else
			assert(type(real) == rtti, path .. " has inconsistent types (" .. type(real) .. " vs " .. rtti .. ")")
		end
	else
		if not rtti then
			assert(false, path .. " missing from type information")
		else
			assert(false, path .. " present in type information but absent from VM")
		end
	end
end

verify(getmetatable(_G).__index, RTTI, "_G")

return 'OK'
