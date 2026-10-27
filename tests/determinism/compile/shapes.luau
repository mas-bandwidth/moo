-- The compiler-determinism corpus (tests/moo/compile_determinism.test.cpp): table shapes, template tables,
-- duplicate record keys, unrolled loops, folded constants, closures and upvalues. Compiled only; never run.

local a = {}
a[1] = 1 a[2] = 2 a[3] = 3 a[4.0] = 4 a[5.5] = 5 a[6] = 6
local b = { x = 1, y = 2, z = 3, x = 4, w = function() end, y = 5 }
local c = setmetatable({}, { __index = b })
c[1] = 1 c[2] = 2
local d = {}
for i = 1, 16 do d[i] = i * 0.25 end
local e = {}
for i = 1, 8.5 do e[i] = i end
local f = { 1, 2, 3, [10] = 10, k = "v", [true] = false }

local total = 0
for i = -3, 9, 4 do
    total += i * 0.1 + i
end

local function counter()
    local n = 0
    return function()
        n += 1
        return n, total
    end
end

local g = counter()
local h = `{a[1]} {b.x} {c[2]} {#d} {#e} {f.k} {g()}`
local k = 2 ^ 10 + 7 // 2 - 9 % 4 + 1e3 / 8 - -3
local m = if k > 100 then "big" else "small"
local s = 0
while s < 10 do
    s += 1
    if s == 5 then continue end
end
repeat s -= 2 until s <= 0

return a, b, c, d, e, f, g, h, k, m, s
