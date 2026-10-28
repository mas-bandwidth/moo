-- This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
-- moo: expression-temporary reuse benchmark (docs/DESIGN.md "Expression-temporary reuse"): the control. The same
-- expression shapes on numbers, which are marked too and must cost what they cost before. Prints objects per
-- iteration (0) and a checksum; bench/moo/tempreuse.py times it.
local n = tonumber((...)) or 20000000

local function seq() return tonumber(string.match(tostring({}), "%d+")) end

local acc, a, b, s = 0, 1, 0.5, 0.001

local before = seq()
for _ = 1, 1000 do
    acc = acc + a*s - b + b*2
end
local objects = (seq() - before - 3) / 1000

for _ = 1, n do
    acc = acc + a*s - b + b*2
    acc = (acc - a*s) * 1 - -(b*s)
end
print("number", objects, acc)
