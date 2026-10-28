-- This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
-- moo: expression-temporary reuse benchmark (docs/DESIGN.md "Expression-temporary reuse"): the DESIGN.md expression
-- acc + a*s - b + b*2 in a loop. Prints the objects created per iteration (from creation numbers, which tostring of a
-- table prints; a probe makes a table and two strings) and a checksum; bench/moo/tempreuse.py times it.
local n = tonumber((...)) or 2000000

local function seq() return tonumber(string.match(tostring({}), "%d+")) end

local acc = vec3.new(0, 0, 0)
local a = vec3.new(1, 2, 3)
local b = vec3.new(0.5, -0.25, 0.125)
local s = 0.001

local before = seq()
for _ = 1, 1000 do
    acc = acc + a*s - b + b*2
end
local objects = (seq() - before - 3) / 1000

for _ = 1, n do
    acc = acc + a*s - b + b*2
end
print("expr", objects, acc.x, acc.y, acc.z)
