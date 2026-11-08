-- This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
-- moo: the completed libraries of layer 3c: the rest of fixed's functions on number, vec3, quat and transform
-- (docs/DESIGN.md "Layer 3c as built: the completed libraries"). The bit-exact comparison with fixed's C is
-- tests/moo/complete.test.cpp; this script pins the behaviour a script sees, on exactly representable values.
-- Each block below is one rule; MooSpecScripts runs this file and expects "OK".

local raw = 1 / 65536 -- one raw unit of Q48.16

local function near(a, b, tolerance) return math.abs(a - b) <= tolerance end
local function samev(v, x, y, z) return v.x == x and v.y == y and v.z == z end
local function nearv(v, x, y, z, tolerance) return near(v.x, x, tolerance) and near(v.y, y, tolerance) and near(v.z, z, tolerance) end
local function nearq(q, x, y, z, w, tolerance)
    return near(q.x, x, tolerance) and near(q.y, y, tolerance) and near(q.z, z, tolerance) and near(q.w, w, tolerance)
end

do
    -- math: muldiv holds the product exact at 128 bits and truncates once, so a product below one raw unit survives
    assert(math.muldiv(3, 4, 2) == 6)
    assert(math.muldiv(raw, raw, raw) == raw)
    assert((raw * raw) / raw == 0) -- the two-rounding form loses it
    assert(math.muldiv(7, 1, 2) == 3.5)
    assert(math.muldiv(-7, 1, 2) == -3.5)
    -- c == 0 saturates with the product's sign; 0 / 0 is 0
    assert(math.muldiv(1, 1, 0) == math.huge)
    assert(math.muldiv(-1, 1, 0) == -math.huge)
    assert(math.muldiv(0, 1, 0) == 0)

    -- halfsum and halfdifference never leave the range
    assert(math.halfsum(3, 5) == 4)
    assert(math.halfsum(3, 4) == 3.5)
    assert(math.halfsum(math.huge, math.huge) == math.huge)
    assert(math.halfsum(-math.huge, -math.huge) == -math.huge)
    assert(math.halfdifference(5, 3) == 1)
    assert(math.halfdifference(3, 5) == -1)
    assert(math.halfdifference(math.huge, -math.huge) == math.huge)

    -- unwindangle: inside [-pi, pi] the angle is unchanged; whole turns come back near zero
    assert(math.unwindangle(0) == 0)
    assert(math.unwindangle(1) == 1)
    assert(math.unwindangle(-3) == -3)
    assert(near(math.unwindangle(math.tau), 0, 0.001))
    assert(near(math.unwindangle(-math.tau), 0, 0.001))
    assert(near(math.unwindangle(math.pi + 1), -math.pi + 1, 0.001))
    for k = -20, 20 do
        local a = math.unwindangle(k * 1.7)
        assert(a >= -math.pi - 0.001 and a <= math.pi + 0.001)
    end

    -- cossin: two values, the precise pair
    local c, s = math.cossin(0)
    assert(c == 1 and s == 0)
    c, s = math.cossin(math.pi / 2)
    assert(near(c, 0, 2 * raw) and near(s, 1, 2 * raw))
    c, s = math.cossin(math.pi)
    assert(near(c, -1, 2 * raw) and near(s, 0, 2 * raw))
    assert(select("#", math.cossin(1)) == 2)

    -- smoothcriticallydamped: the position and the velocity come back as two values; a non-positive smooth time
    -- jumps to the target and a non-positive step stays put, as fixed documents
    local p, v = math.smoothcriticallydamped(0, 10, 0, 0, 0.1)
    assert(p == 10 and v == 0)
    p, v = math.smoothcriticallydamped(0, 10, 3, 1, 0)
    assert(p == 0 and v == 3)
    p, v = math.smoothcriticallydamped(0, 10, 0, 1, 0.1)
    assert(p > 0 and p < 10 and v > 0)
    local pu, vu = math.smoothcriticallydampedupdown(0, 10, 0, 1, 1, 0.1)
    assert(pu == p and vu == v)
    -- moving down (toward a smaller magnitude) takes the second time
    local pd, vd = math.smoothcriticallydampedupdown(10, 0, 0, 1, 0.5, 0.1)
    local pd2, vd2 = math.smoothcriticallydamped(10, 0, 0, 0.5, 0.1)
    assert(pd == pd2 and vd == vd2)
    -- the sequence settles: after many steps the position is at the target within a few raw units
    local x, vel = 0, 0
    for _ = 1, 400 do
        x, vel = math.smoothcriticallydamped(x, 10, vel, 0.5, 1 / 60)
    end
    assert(near(x, 10, 16 * raw), tostring(x))

    -- the integer raw-value helpers read the number's raw bits
    assert(math.fits(0.5, 0, 1) == true)
    assert(math.fits(0, 0, 1) == true and math.fits(1, 0, 1) == true)
    assert(math.fits(2, 0, 1) == false and math.fits(-raw, 0, 1) == false)
    assert(math.narrow(1, 16) == raw)        -- 65536 raw >> 16
    assert(math.narrow(0.5, 16) == raw)      -- half rounds up
    assert(math.narrow(0.5 - raw, 16) == 0)
    assert(math.narrow(-0.5, 16) == 0)       -- half rounds toward positive infinity
    assert(math.narrow(-0.5 - raw, 16) == -raw)
    assert(math.narrow(5, 0) == 5)
    assert(math.widen(raw, 16) == 1)
    assert(math.widen(-3 * raw, 16) == -3)
    assert(math.widen(5, 0) == 5)
    assert(math.narrow(math.widen(7 * raw, 20), 20) == 7 * raw)
    assert(not pcall(math.narrow, 1, 63))
    assert(not pcall(math.widen, 2147483648, 16)) -- 2^47 raw << 16 does not fit the asserted range
    assert(not pcall(math.narrow, math.huge, 1))
    assert(not pcall(math.narrow, 1, 0.5))
end

do
    -- vec3
    local a = vec3.new(1, 2, 3)
    local b = vec3.new(4, 5, 6)

    assert(vec3.distance(a, b) == vec3.length(b - a))
    assert(vec3.distance(vec3.new(0, 0, 0), vec3.new(3, 4, 0)) == 5)
    assert(vec3.distancesquared(a, b) == 27)
    assert(vec3.lengthsquared(a) == 14)
    assert(vec3.lengthsquared(vec3.new()) == 0)

    -- lerp refuses an alpha outside [0, 1], as quat.nlerp does
    assert(vec3.lerp(a, b, 0) == a)
    assert(vec3.lerp(a, b, 1) == b)
    assert(samev(vec3.lerp(a, b, 0.5), 2.5, 3.5, 4.5))
    assert(not pcall(vec3.lerp, a, b, 2))
    assert(not pcall(vec3.lerp, a, b, -raw))

    assert(samev(vec3.min(vec3.new(1, 5, -3), vec3.new(2, 4, -4)), 1, 4, -4))
    assert(samev(vec3.max(vec3.new(1, 5, -3), vec3.new(2, 4, -4)), 2, 5, -3))
    assert(samev(vec3.clamp(vec3.new(-5, 0.5, 9), vec3.new(0, 0, 0), vec3.new(1, 1, 1)), 0, 0.5, 1))
    assert(samev(vec3.abs(vec3.new(-1, 2, -3.5)), 1, 2, 3.5))
    -- sign is -1 or 1, and 1 for a zero component (fixed's convention)
    assert(samev(vec3.sign(vec3.new(0, -1, 2)), 1, -1, 1))

    -- perp is a unit vector at a right angle
    assert(samev(vec3.perp(vec3.new(1, 0, 0)), 0, -1, 0))
    assert(samev(vec3.perp(vec3.new(0, 1, 0)), 0, 0, -1))
    local p = vec3.perp(vec3.normalize(a))
    assert(vec3.isnormalized(p) and near(vec3.dot(p, a), 0, 0.001))

    -- muladd, mulsub and blend2 are a + s * b, a - s * b and s * a + t * b
    assert(vec3.muladd(a, 2, b) == a + 2 * b)
    assert(vec3.mulsub(a, 2, b) == a - 2 * b)
    assert(vec3.blend2(2, a, 3, b) == 2 * a + 3 * b)
    assert(samev(vec3.blend2(0.5, a, 0.5, b), 2.5, 3.5, 4.5))

    -- safescale keeps every axis at least fixed's minimum scale from zero, sign preserved
    local safe = vec3.safescale(vec3.new(-5, 0.001, 2))
    assert(safe.x == -5 and safe.z == 2 and safe.y > 0.0099 and safe.y < 0.0101)
    local zero = vec3.safescale(vec3.new())
    assert(zero.x == zero.y and zero.y == zero.z and zero.x > 0.0099 and zero.x < 0.0101)

    -- isnormalized, and the two normalize-with-length forms (two values)
    assert(vec3.isnormalized(vec3.new(1, 0, 0)) == true)
    assert(vec3.isnormalized(a) == false)
    assert(vec3.isnormalized(vec3.normalize(a)) == true)
    local unit, length = vec3.getlengthandnormalize(vec3.new(3, 4, 0))
    assert(length == 5 and nearv(unit, 0.6, 0.8, 0, 2 * raw) and vec3.isnormalized(unit))
    unit, length = vec3.normalizewithlength(vec3.new(3, 4, 0))
    assert(length == 5 and nearv(unit, 0.6, 0.8, 0, 2 * raw) and vec3.isnormalized(unit))
    unit, length = vec3.getlengthandnormalize(vec3.new())
    assert(length == 0 and unit == vec3.new())
    unit, length = vec3.normalizewithlength(vec3.new())
    assert(length == 0 and unit == vec3.new())
    assert(select("#", vec3.getlengthandnormalize(a)) == 2)

    -- the position family: positions are vec3s at this pin
    assert(vec3.subpos(b, a) == b - a)
    assert(vec3.offsetpos(a, b) == a + b)
    assert(samev(vec3.lerpposition(a, b, 0.5), 2.5, 3.5, 4.5))
    assert(vec3.lerpposition(a, b, 0) == a and vec3.lerpposition(a, b, 1) == b)
    assert(pcall(vec3.lerpposition, a, b, 2)) -- fixed asserts nothing about t here
    assert(vec3.isvalid(a) == true)
end

do
    -- quat
    local id = quat.new()
    local half = quat.new(0, 0, 1, 0) -- a half turn about z, exactly representable
    local halfx = quat.new(1, 0, 0, 0)

    assert(quat.normalize(quat.new(0, 0, 2, 0)) == half)
    assert(quat.normalize(id) == id)
    assert(quat.normalize(quat.new(0, 0, 0, 0)) == id) -- fixed's answer for the zero quaternion

    -- invmul is inv(a) * b
    assert(quat.invmul(half, id) == quat.conjugate(half))
    assert(quat.invmul(id, half) == half)
    assert(quat.invmul(half, half) == id)

    assert(quat.dot(half, half) == 1)
    assert(quat.dot(id, half) == 0)
    assert(quat.dot(id, quat.negate(id)) == -1)
    assert(quat.negate(id) == quat.new(0, 0, 0, -1))
    assert(quat.negate(quat.negate(half)) == half)

    -- fromaxisangle needs a unit axis; the angle is fixComputeCosSin's half angle
    assert(quat.fromaxisangle(vec3.new(0, 0, 1), 0) == id)
    local q = quat.fromaxisangle(vec3.new(0, 0, 1), math.pi)
    assert(nearq(q, 0, 0, 1, 0, 0.003))
    assert(not pcall(quat.fromaxisangle, vec3.new(0, 0, 2), 1))
    assert(not pcall(quat.fromaxisangle, vec3.new(), 1))

    -- axisangle, angle, twist and swing
    local axis, radians = quat.axisangle(half)
    assert(axis == vec3.new(0, 0, 1) and near(radians, math.pi, 0.001))
    axis, radians = quat.axisangle(id)
    assert(axis == vec3.new() and radians == 0)
    assert(quat.angle(id) == 0)
    assert(near(quat.angle(half), math.pi, 0.001))
    assert(near(quat.twistangle(half), math.pi, 0.001))
    assert(quat.twistangle(halfx) == 0)
    assert(quat.swingangle(half) == 0)
    assert(near(quat.swingangle(halfx), math.pi, 0.001))
    assert(quat.twistangle(id) == 0 and quat.swingangle(id) == 0)

    -- betweenunitvectors: the rotation that carries one unit vector onto another
    local x, y = vec3.new(1, 0, 0), vec3.new(0, 1, 0)
    local r = quat.betweenunitvectors(x, y)
    assert(quat.isnormalized(r))
    assert(nearv(r * x, 0, 1, 0, 0.001))
    assert(quat.betweenunitvectors(x, x) == id)
    local back = quat.betweenunitvectors(x, -x) -- the anti-parallel branch
    assert(quat.isnormalized(back) and nearv(back * x, -1, 0, 0, 0.001))
    assert(not pcall(quat.betweenunitvectors, x, vec3.new(0, 2, 0)))
    assert(not pcall(quat.betweenunitvectors, vec3.new(0, 2, 0), x))

    -- validity includes normalization
    assert(quat.isvalid(id) == true and quat.isnormalized(id) == true)
    assert(quat.isvalid(quat.new(0, 0, 2, 0)) == false and quat.isnormalized(quat.new(0, 0, 2, 0)) == false)
end

do
    -- transform
    local half = quat.new(0, 0, 1, 0)
    local a = transform.new(vec3.new(1, 2, 3), half)
    local b = transform.new(vec3.new(4, 5, 6))

    -- invmul is the transform of frame b in frame a; exact here, so it equals the composed form
    local rel = transform.invmul(a, b)
    assert(rel.p == vec3.new(-3, -3, 3) and rel.q == quat.conjugate(half))
    assert(rel == transform.inverse(a) * b)
    assert(transform.invmul(a, a) == transform.new())

    -- the world family: fixWorldTransform and fixPos are transform and vec3 at this pin
    assert(transform.makeworld(a) == a)
    assert(transform.mulworld(a, b) == a * b)
    assert(transform.invmulworld(a, b) == rel)
    local v = vec3.new(7, 8, 9)
    assert(transform.transformworldpoint(a, v) == a * v)
    assert(transform.invtransformworldpoint(a, v) == transform.invpoint(a, v))
    assert(transform.invtransformworldpoint(a, transform.transformworldpoint(a, v)) == v)
    assert(transform.torelative(a, vec3.new(1, 1, 1)) == transform.new(vec3.new(0, 1, 2), half))

    assert(transform.isvalid(a) == true)
    assert(transform.isvalid(transform.new(nil, quat.new(0, 0, 2, 0))) == false)
end

return "OK"
