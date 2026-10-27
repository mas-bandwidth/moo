-- This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
-- moo: vec3: the object, its layout and tag, value equality, table keys, GC, and the operators and library
-- against fixed's C (docs/DESIGN.md "vec3, quat and transform").
-- Each block below is one rule; MooSpecScripts runs this file and expects "OK".

do
    -- moo: vec3 operators and library (docs/DESIGN.md "vec3, quat and transform (layer 3b)", "Operations").
    -- Every case is a value comparison against fixed's documented result; the edge at the end runs in release and
    -- checked builds alike, as tests/moo/number.test.cpp does for number.
    local vec3 = vec3

    -- a vec3's components read as v.x, v.y and v.z (and X, Y, Z, as Luau's vector)
    local function same(v, x, y, z) return v.x == x and v.y == y and v.z == z end

    assert(tostring(vec3.new(1, -2.5, 0.1)) == "vec3(1, -2.5, 0.1)")
    assert(vec3.new(4, 5, 6).Y == 5)
    assert(not pcall(function(v) v.x = 1 end, vec3.new()))
    assert(type(vec3.new(1, 2, 3)) == "vec3")
    assert(typeof(vec3.new(1, 2, 3)) == "vec3")
    assert(same(vec3.new(1, 2, 3), 1, 2, 3))
    assert(same(vec3.new(), 0, 0, 0))
    assert(same(vec3.create(1, 2, 3), 1, 2, 3))

    local a = vec3.new(1, 2, 3)
    local b = vec3.new(4, 5, 6)

    -- +
    assert(same(a + b, 5, 7, 9))
    assert(same(a + vec3.new(0, 0, 0), 1, 2, 3))
    -- -
    assert(same(b - a, 3, 3, 3))
    assert(same(a - b, -3, -3, -3))
    -- unary -
    assert(same(-a, -1, -2, -3))
    assert(same(-(-a), 1, 2, 3))
    -- * vec3, component-wise
    assert(same(a * b, 4, 10, 18))
    assert(same(a * vec3.new(2, 2, 2), 2, 4, 6))
    -- * number and number *
    assert(same(a * 2, 2, 4, 6))
    assert(same(2 * a, 2, 4, 6))
    assert(same(a * 0.5, 0.5, 1, 1.5))
    -- == is by value, not by address
    assert(a == vec3.new(1, 2, 3))
    assert(not (a == b))
    assert(a ~= b)
    assert(not (a ~= vec3.new(1, 2, 3)))
    assert(not rawequal(a + b, a))
    assert(rawequal(a, a))

    -- dot, cross, length, normalize
    assert(vec3.dot(a, b) == 32)
    assert(vec3.dot(b, a) == 32)
    assert(same(vec3.cross(vec3.new(1, 0, 0), vec3.new(0, 1, 0)), 0, 0, 1))
    assert(vec3.length(vec3.new(3, 4, 0)) == 5)
    assert(vec3.magnitude(vec3.new(3, 4, 0)) == 5)
    assert(vec3.length(vec3.new(0, 0, 0)) == 0)
    -- fixed's fixNormalize divides by the integer square root of the exact 128-bit sum of squares; its result is
    -- therefore slightly below the rounded 0.6/0.8, and the bits are fixed's (DESIGN.md "Saturation, stated precisely")
    assert(same(vec3.normalize(vec3.new(3, 4, 0)), 0.59999, 0.79999, 0))
    assert(vec3.length(vec3.normalize(vec3.new(3, 4, 0))) == 0.99998)

    -- a + and - edge: release saturates, a checked build raises the named error at the operation
    local max = moo_test.fromraw("9223372036854775807")
    local min = moo_test.fromraw("-9223372036854775807")
    local step = moo_test.fromraw("1")
    local checked = not pcall(function(x, y) return x + y end, max, step)

    local function edge(name, fn, wantx, wanty, wantz, x, y)
        local ok, result = pcall(fn, x, y)
        if checked then
            assert(not ok, name .. " did not raise")
            local suffix = "number overflow in '" .. name .. "'"
            assert(string.sub(result, -#suffix) == suffix, result)
        else
            assert(ok, result)
            assert(same(result, wantx, wanty, wantz), name .. ": " .. tostring(result))
        end
    end

    local big = vec3.new(max, min, max)
    local one = vec3.new(step, -step, 0)
    edge("+", function(x, y) return x + y end, max, min, max, big, one)
    edge("-", function(x, y) return x - y end, max, min, max, big, vec3.new(-step, step, 0))

    -- inside the edge both builds give the same value
    assert(same(vec3.new(max - step, min + step, 0) + one, max, min, 0))
end

do
    -- moo: layer 3b vec3 as a table key (moo-v2-l3b-vec3-object-key).
    --
    -- A vec3 cannot be built from script yet: the script constructor is card
    -- moo-v2-l3b-vec3-ops (moo_pushvec3/moo_tovec3). Until that lands, the C++ test
    -- (tests/moo/vec3.test.cpp) exercises equal vec3 keys, the frozen
    -- component hash, the deterministic iteration order and GC survival through the internal
    -- constructors. This script pins the contract that the moo_test helpers are present and
    -- return "OK"; the vec3 exercising above is the vec3 rule set in docs/DESIGN.md
    -- "vec3, quat and transform".
    assert(type(moo_test) == "table")
    assert(type(moo_test.raw) == "function")
    assert(type(moo_test.fromraw) == "function")
    assert(moo_test.raw(0) == "0")
    assert(moo_test.fromraw("0") == 0)
end

return "OK"
