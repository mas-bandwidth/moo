-- This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
-- moo: expression-temporary reuse benchmark (docs/DESIGN.md "Expression-temporary reuse"): quat and transform
-- chains: an orientation integrated by two rotations, a transform composed through three, and a point moved by a
-- transform after an offset. Prints objects per iteration and a checksum; bench/moo/tempreuse.py times it.
local n = tonumber((...)) or 1000000

local function seq() return tonumber(string.match(tostring({}), "%d+")) end

local q = quat.new(0, 0, 0, 1)
local dq1 = quat.nlerp(quat.new(0, 0, 0, 1), quat.new(0, 0.7071, 0, 0.7071), 0.01)
local dq2 = quat.nlerp(quat.new(0, 0, 0, 1), quat.new(0.7071, 0, 0, 0.7071), 0.01)
local t = transform.new()
local u = transform.new(vec3.new(0.01, 0, 0), dq1)
local w = transform.new(vec3.new(0, 0.01, 0), dq2)
local p = vec3.new(1, 2, 3)
local off = vec3.new(0.001, 0, 0)

local function iter()
    q = q * dq1 * dq2
    t = t * u * w * u
    p = q * (p + off * 2)
end

local before = seq()
for _ = 1, 1000 do iter() end
local objects = (seq() - before - 3) / 1000

for _ = 1, n do iter() end
print("rotate", objects, q.w, t.p.x, p.x)
