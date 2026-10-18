-- moo: samples the analyzer's builtin seam (docs/DESIGN.md "Analyzer"). The analyzer's definitions
-- drop os, vector and integer; moo-analyze reports vector.create as an unknown global (DONE-WHEN).
-- The VM side pinned here is that os, vector and integer are all absent; moo-l3-libs-out removed
-- luaopen_vector, so types.luau no longer ignores _G.vector.
assert(rawget(_G, "os") == nil, "os must not be a global")
assert(rawget(_G, "integer") == nil, "integer must not be a global")
assert(rawget(_G, "vector") == nil, "vector must not be a global")

return "OK"
