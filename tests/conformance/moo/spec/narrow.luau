-- This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
-- moo: layer 3c N2, the operators, component reads and libraries of vec2, mat3, plane, aabb and fix30
-- (docs/DESIGN.md "Layer 3c as built"). The bit-exact comparison with fixed's C is tests/moo/complete.test.cpp; this
-- script pins the behaviour a script sees, on exactly representable values. MooSpecScripts runs this file and
-- expects "OK".

local function samev(v, x, y, z) return v.x == x and v.y == y and v.z == z end

do
    -- vec2: two components, no operators of its own (fixed has none)
    local v = vec2.new(1.5, -2)
    assert(v.x == 1.5 and v.y == -2 and v.X == 1.5 and v.Y == -2)
    assert(vec2.new() == vec2.new(0, 0))
    assert(typeof(v) == "vec2")
    assert(not pcall(function() return v + v end))
end

do
    -- mat3: three vec3 columns; mat3.new() is the identity
    local i = mat3.new()
    assert(samev(i.cx, 1, 0, 0) and samev(i.cy, 0, 1, 0) and samev(i.cz, 0, 0, 1))
    local m = mat3.new(vec3.new(1, 2, 3), vec3.new(4, 5, 6), vec3.new(7, 8, 10))
    assert(typeof(m) == "mat3")
    assert(m * i == m and i * m == m)
    assert(m + i == mat3.new(vec3.new(2, 2, 3), vec3.new(4, 6, 6), vec3.new(7, 8, 11)))
    assert(m - m == mat3.new(vec3.new(), vec3.new(), vec3.new()))
    assert(-m == m * -1 and -m == -1 * m)
    assert(m * 2 == m + m)
    -- column-major: m * v is v.x * cx + v.y * cy + v.z * cz
    assert(m * vec3.new(1, 0, 0) == m.cx and m * vec3.new(0, 0, 1) == m.cz)
    assert(m * vec3.new(1, 1, 1) == vec3.new(12, 15, 19))
    assert(mat3.transpose(mat3.transpose(m)) == m)
    assert(mat3.transpose(m).cx == vec3.new(1, 4, 7))
    assert(mat3.det(m) == -3)
    assert(mat3.det(i) == 1)
    assert(mat3.abs(-i) == i)
    assert(mat3.inverse(i) == i and mat3.inversewide(i) == i and mat3.inverset(i) == i)
    local d = mat3.new(vec3.new(2, 0, 0), vec3.new(0, 4, 0), vec3.new(0, 0, 8))
    assert(mat3.inverse(d) == mat3.new(vec3.new(0.5, 0, 0), vec3.new(0, 0.25, 0), vec3.new(0, 0, 0.125)))
    assert(mat3.solve(d, vec3.new(1, 1, 1)) == vec3.new(0.5, 0.25, 0.125))
    assert(mat3.solvewide(d, vec3.new(1, 1, 1)) == vec3.new(0.5, 0.25, 0.125))
    -- a singular matrix has no inverse: fixed's answer is the zero matrix
    local s = mat3.new(vec3.new(1, 2, 3), vec3.new(1, 2, 3), vec3.new(0, 0, 1))
    assert(mat3.det(s) == 0)
    assert(mat3.inverse(s) == mat3.new(vec3.new(), vec3.new(), vec3.new()))
    assert(mat3.isvalid(m))
    -- a rotation round trips through quat: the identity exactly
    assert(mat3.fromquat(quat.new()) == i)
    assert(quat.frommatrix(i) == quat.new())
    -- mat3 * mat3 and m * v agree
    local v = vec3.new(1, -1, 2)
    assert((m * d) * v == m * (d * v))
    -- equal bits are one key
    local t = {}
    t[m] = 1
    t[mat3.new(vec3.new(1, 2, 3), vec3.new(4, 5, 6), vec3.new(7, 8, 10))] = 2
    local n = 0
    for _ in pairs(t) do n += 1 end
    assert(n == 1 and t[m] == 2)
end

do
    -- plane: a unit normal and an offset
    local p = plane.new(vec3.new(0, 1, 0), 3)
    assert(typeof(p) == "plane")
    assert(samev(p.normal, 0, 1, 0) and p.offset == 3)
    assert(plane.isvalid(p))
    assert(plane.new(vec3.new(0, 0, 1)).offset == 0)
    assert(not pcall(plane.new, vec3.new(0, 2, 0), 1))
end

do
    -- aabb: lower and upper corners
    local a = aabb.new(vec3.new(-1, -2, -3), vec3.new(1, 2, 3))
    assert(typeof(a) == "aabb")
    assert(samev(a.lower, -1, -2, -3) and samev(a.upper, 1, 2, 3))
    assert(aabb.center(a) == vec3.new())
    assert(aabb.extents(a) == vec3.new(1, 2, 3))
    assert(aabb.area(a) == 88) -- 2 * (2*4 + 4*6 + 6*2)
    assert(aabb.contains(a, aabb.new(vec3.new(), vec3.new(1, 1, 1))))
    assert(not aabb.contains(aabb.new(vec3.new(), vec3.new(1, 1, 1)), a))
    local b = aabb.new(vec3.new(0, 0, 0), vec3.new(5, 5, 5))
    assert(aabb.overlaps(a, b) and aabb.overlaps(b, a))
    assert(not aabb.overlaps(a, aabb.new(vec3.new(4, 4, 4), vec3.new(5, 5, 5))))
    assert(aabb.union(a, b) == aabb.new(vec3.new(-1, -2, -3), vec3.new(5, 5, 5)))
    assert(aabb.inflate(a, 1) == aabb.new(vec3.new(-2, -3, -4), vec3.new(2, 3, 4)))
    assert(aabb.frompoints({vec3.new(1, 2, 3), vec3.new(-1, 5, 0)}) == aabb.new(vec3.new(-1, 2, 0), vec3.new(1, 5, 3)))
    assert(aabb.frompoints({vec3.new(1, 2, 3)}, 1) == aabb.new(vec3.new(0, 1, 2), vec3.new(2, 3, 4)))
    assert(aabb.closestpoint(vec3.new(10, 0, -10), a) == vec3.new(1, 0, -3))
    assert(aabb.closestpoint(vec3.new(), a) == vec3.new())
    -- an identity rotation moves the box by the translation
    local t = transform.new(vec3.new(1, 1, 1), quat.new())
    assert(aabb.transform(t, a) == aabb.new(vec3.new(0, -1, -2), vec3.new(2, 3, 4)))
    assert(aabb.fromcenterextents(t, vec3.new(), vec3.new(1, 2, 3)) == aabb.transform(t, a))
    assert(aabb.isvalid(a))
    assert(not pcall(aabb.new, vec3.new(1, 0, 0), vec3.new(0, 0, 0)))
    assert(not pcall(aabb.frompoints, {}))
end

do
    -- fix30: a Q2.30 fraction in [-2, 2)
    local h = fix30.new(0.5)
    assert(typeof(h) == "fix30")
    assert(fix30.tonumber(h) == 0.5)
    assert(fix30.new(1) == fix30.new(1))
    assert(fix30.tonumber(fix30.new(-1.25)) == -1.25)
    -- x / length on the raw bits, rounded at 2^-30
    assert(fix30.tonumber(fix30.normalizecomponent(3, 4)) == 0.75)
    assert(fix30.tonumber(fix30.normalizecomponent(-4, 4)) == -1)
    assert(not pcall(fix30.normalizecomponent, 1, 0))
    local t = {}
    t[fix30.new(0.25)] = true
    assert(t[fix30.new(0.25)] == true)
end

return "OK"
