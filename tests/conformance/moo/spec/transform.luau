-- This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
-- moo: transform: the object, value behaviour, GC, and the operations against fixed's C (docs/DESIGN.md
-- "vec3, quat and transform").
-- Each block below is one rule; MooSpecScripts runs this file and expects "OK".

do
    -- moo: transform object, operators and library (docs/DESIGN.md "vec3, quat and transform (layer 3b)", "Operations";
    -- docs/history/layer3b/moo-v2-l3b-transform.md). The bit-exact comparison with fixed is in
    -- tests/moo/transform.test.cpp; this script pins the behaviour a script sees.

    -- construction, parts, type and tostring
    local id = transform.new()
    assert(type(id) == "transform" and typeof(id) == "transform")
    assert(id.p == vec3.new(0, 0, 0) and id.q == quat.new())
    assert(tostring(id) == "transform(vec3(0, 0, 0), quat(0, 0, 0, 1))")
    local half = quat.new(0, 0, 1, 0) -- a half turn about z, exactly representable
    local t = transform.new(vec3.new(1, 2, 3), half)
    assert(t.p == vec3.new(1, 2, 3) and t.q == half)
    assert(tostring(t) == "transform(vec3(1, 2, 3), quat(0, 0, 1, 0))")
    assert(not pcall(function(x) x.p = vec3.new(0, 0, 0) end, t))

    -- equality is by value, and a transform is a table key by value
    assert(t == transform.new(vec3.new(1, 2, 3), half))
    assert(t ~= transform.new(vec3.new(1, 2, 3)))
    local keys = {}
    keys[t] = "frame"
    assert(keys[transform.new(vec3.new(1, 2, 3), quat.new(0, 0, 1, 0))] == "frame")

    -- the identity leaves a transform and a point exactly as they are
    assert(id * t == t and t * id == t)
    local v = vec3.new(4, 5, 6)
    assert(id * v == v)

    -- point: rotate, then translate; direction: rotate only
    assert(t * vec3.new(1, 0, 0) == vec3.new(0, 2, 3))
    assert(transform.point(t, vec3.new(1, 0, 0)) == vec3.new(0, 2, 3))
    assert(transform.direction(t, vec3.new(1, 0, 0)) == vec3.new(-1, 0, 0))

    -- the inverse undoes the point and the direction exactly for this rotation
    assert(transform.invpoint(t, vec3.new(0, 2, 3)) == vec3.new(1, 0, 0))
    assert(transform.invdirection(t, vec3.new(-1, 0, 0)) == vec3.new(1, 0, 0))
    local inv = transform.inverse(t)
    assert(inv * (t * v) == v)
    assert(inv * t == transform.new(vec3.new(0, 0, 0), quat.new(0, 0, 0, 1)))

    -- composition is applied right to left: (a * b) * v == a * (b * v)
    local a = transform.new(vec3.new(10, 0, 0), half)
    local b = transform.new(vec3.new(0, 1, 0))
    assert((a * b) * v == a * (b * v))
    assert(a * b ~= b * a)
end

return "OK"
