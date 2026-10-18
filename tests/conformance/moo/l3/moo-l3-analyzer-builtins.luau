-- moo: samples the analyzer's builtin seam (docs/DESIGN.md "Analyzer"). The analyzer's definitions
-- drop os, vector and integer; moo-analyze reports vector.create as an unknown global (DONE-WHEN).
-- The VM side pinned here is that os and integer are absent while vector is still opened at this
-- base, so types.luau ignores _G.vector until the VM removal lands.
assert(rawget(_G, "os") == nil, "os must not be a global")
assert(rawget(_G, "integer") == nil, "integer must not be a global")
assert(type(rawget(_G, "vector")) == "table", "the VM still opens vector at this base")

return "OK"
