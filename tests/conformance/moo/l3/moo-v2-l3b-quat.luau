-- moo: quat operators and library (docs/DESIGN.md "vec3, quat and transform (layer 3b)", "Operations";
-- docs/layer3b/moo-v2-l3b-quat.md). The bit-exact comparison with fixed is in tests/moo/moo-v2-l3b-quat.test.cpp;
-- this script pins the behaviour a script sees.
local function same(q, x, y, z, w) return q.x == x and q.y == y and q.z == z and q.w == w end

-- construction, components, type and tostring
local id = quat.new()
assert(same(id, 0, 0, 0, 1))
assert(type(id) == "quat" and typeof(id) == "quat")
assert(tostring(quat.new(0, 0, 0.6, 0.8)) == "quat(0, 0, 0.6, 0.8)")
assert(not pcall(function(q) q.w = 0 end, id))

-- equality is by value, and a quat is a table key by value
local q = quat.new(0, 0, 0.6, 0.8)
assert(q == quat.new(0, 0, 0.6, 0.8))
assert(q ~= quat.new(0, 0, 0.6, -0.8))
local t = {}
t[q] = "turn"
assert(t[quat.new(0, 0, 0.6, 0.8)] == "turn")

-- the identity leaves a quat and a vec3 exactly as they are
assert(id * q == q and q * id == q)
local v = vec3.new(1, 2, 3)
assert(id * v == v)

-- a quarter turn about z, with exactly representable components: (0, 0, 1/2, 1/2) scaled to unit length is
-- not exact, so use the half-turn (0, 0, 1, 0), which is
local half = quat.new(0, 0, 1, 0)
assert(half * vec3.new(1, 0, 0) == vec3.new(-1, 0, 0))
assert(half * vec3.new(0, 1, 0) == vec3.new(0, -1, 0))
assert(half * vec3.new(0, 0, 1) == vec3.new(0, 0, 1))
assert(half * half == quat.new(0, 0, 0, -1))

-- conjugate negates the vector part
assert(quat.conjugate(q) == quat.new(0, 0, -0.6, 0.8))
assert(quat.conjugate(quat.conjugate(q)) == q)

-- nlerp: the end points of a blend between the identity and the half turn are unit quaternions on the path
assert(quat.nlerp(id, half, 0) == id)
assert(quat.nlerp(id, half, 1) == half)
local mid = quat.nlerp(id, half, 0.5)
assert(mid.x == 0 and mid.y == 0 and mid.z == mid.w and mid.z > 0.707 and mid.z < 0.7072)
assert(not pcall(quat.nlerp, id, half, 2))

-- slerp waits for fixed
local ok, err = pcall(quat.slerp, id, half, 0.5)
assert(not ok and string.find(err, "quat.slerp waits for fixed's slerp", 1, true))

return "OK"
