-- This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
-- moo: the VM side of the analyzer's vec3, quat and transform types (tests/MooAnalyzer.test.cpp).
-- Each block below is one rule; MooSpecScripts runs this file and expects "OK".

do
	-- moo: the VM side of tests/MooAnalyzer.test.cpp (docs/history/layer3b/moo-v2-l3b-analyzer-types.md): the program the
	-- analyzer accepts with no error runs, and each value has the type the analyzer gave it.
	local a = vec3.new(1, 2, 3)
	local b = vec3.create(4, 5)
	local z = vec3.new()
	local c = a + b - a * b * 2 + (-a)
	local d = 2 * a
	local n = vec3.dot(a, b) + vec3.length(a) + vec3.magnitude(b) + a.x + a.y + a.z + a.X + a.Y + a.Z
	local e = vec3.cross(a, b)
	local f = vec3.normalize(e)
	local q = quat.new(0, 0, 0.6, 0.8)
	local q0 = quat.new()
	local r = q * quat.conjugate(q) * quat.nlerp(q, q0, 0.5)
	local g = q * f
	local w = q.x + q.y + q.z + q.w + q.X + q.Y + q.Z + q.W
	local t = transform.new(a, q)
	local t0 = transform.new()
	local t1 = transform.new(nil, q)
	local u = t * transform.inverse(t) * t0 * t1
	local h = t * a
	local p = t.p
	local qq = t.q
	local k = transform.point(t, a) + transform.direction(t, a) + transform.invpoint(t, a) + transform.invdirection(t, a)
	local same = a == b and q == r and t == u

	for _, v in {a, b, z, c, d, e, f, g, h, p, k} do
		assert(typeof(v) == "vec3")
	end
	for _, v in {q, q0, r, qq} do
		assert(typeof(v) == "quat")
	end
	for _, v in {t, t0, t1, u} do
		assert(typeof(v) == "transform")
	end
	assert(type(n) == "number" and type(w) == "number" and type(same) == "boolean")
	assert(typeof(a) .. typeof(q) .. typeof(t) == "vec3quattransform")
end

return "OK"
