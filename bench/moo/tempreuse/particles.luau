-- This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
-- moo: expression-temporary reuse benchmark (docs/DESIGN.md "Expression-temporary reuse"): explicit Euler steps of
-- 64 particles with gravity and drag, positions and velocities in tables. Prints objects per particle step and a
-- checksum; bench/moo/tempreuse.py times it.
local n = tonumber((...)) or 20000

local function seq() return tonumber(string.match(tostring({}), "%d+")) end

local count = 64
local pos, vel = {}, {}
for i = 1, count do
    pos[i] = vec3.new(i, 0, -i)
    vel[i] = vec3.new(0.5, i * 0.01, 0)
end
local g = vec3.new(0, -9.75, 0)
local dt = 1 / 64
local drag = 0.01

local function step()
    for i = 1, count do
        local v = vel[i]
        v = v + g * dt - v * drag
        vel[i] = v
        pos[i] = pos[i] + v * dt + g * (dt * dt * 0.5)
    end
end

local before = seq()
for _ = 1, 10 do step() end
local objects = (seq() - before - 3) / (10 * count)

for _ = 1, n do step() end
local sum = vec3.new(0, 0, 0)
for i = 1, count do sum += pos[i] end
print("particles", objects, sum.x, sum.y, sum.z)
