// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: vec3, quat and transform as analyzer builtin types (Analysis/src/EmbeddedBuiltinDefinitions.cpp,
// kBuiltinDefinitionMooValueTypesSrc; docs/history/layer3b/moo-v2-l3b-analyzer-types.md). The analyzer accepts every
// component, operator and library function the VM implements, and reports each one the VM refuses. The VM side of
// the same program is tests/conformance/moo/spec/analyzer.luau (Moo.Test), and
// tests/conformance/types.luau checks that the VM's globals and these definitions name the same functions.
#include "Fixture.h"

#include "doctest.h"

#include <string>

using namespace Luau;

TEST_SUITE_BEGIN("MooAnalyzerValueTypes");

// Every component, operator and library function of the three types, with the result types the VM produces.
TEST_CASE_FIXTURE(BuiltinsFixture, "moo_value_types_accept_everything_the_vm_implements")
{
    CheckResult result = check(R"(
--!strict
local a: vec3 = vec3.new(1, 2, 3)
local b: vec3 = vec3.create(4, 5)
local z: vec3 = vec3.new()
local c: vec3 = a + b - a * b * 2 + (-a)
local d: vec3 = 2 * a
local n: number = vec3.dot(a, b) + vec3.length(a) + vec3.magnitude(b) + a.x + a.y + a.z + a.X + a.Y + a.Z
local e: vec3 = vec3.cross(a, b)
local f: vec3 = vec3.normalize(e)
local q: quat = quat.new(0, 0, 0.6, 0.8)
local q0: quat = quat.new()
local r: quat = q * quat.conjugate(q) * quat.nlerp(q, q0, 0.5)
local g: vec3 = q * f
local w: number = q.x + q.y + q.z + q.w + q.X + q.Y + q.Z + q.W
local t: transform = transform.new(a, q)
local t0: transform = transform.new()
local t1: transform = transform.new(nil, q)
local u: transform = t * transform.inverse(t) * t0 * t1
local h: vec3 = t * a
local p: vec3 = t.p
local qq: quat = t.q
local k: vec3 = transform.point(t, a) + transform.direction(t, a) + transform.invpoint(t, a) + transform.invdirection(t, a)
local same: boolean = a == b and q == r and t == u
local names: string = typeof(a) .. typeof(q) .. typeof(t)
local _ = {c, d, n, z, g, w, h, p, qq, k, same, names}
)");

    LUAU_REQUIRE_NO_ERRORS(result);
}

// Layer 3c: the rest of fixed's functions on number, vec3, quat and transform (docs/DESIGN.md "Layer 3c as built"),
// with the two-value returns typed as such.
TEST_CASE_FIXTURE(BuiltinsFixture, "moo_value_types_accept_the_completed_libraries")
{
    CheckResult result = check(R"(
--!strict
local n: number = math.muldiv(1, 2, 3) + math.halfsum(1, 2) + math.halfdifference(1, 2) + math.unwindangle(7)
local c: number, s: number = math.cossin(1)
local p: number, v: number = math.smoothcriticallydamped(0, 1, 0, 1, 0.1)
local p2: number, v2: number = math.smoothcriticallydampedupdown(0, 1, 0, 1, 2, 0.1)
local fits: boolean = math.fits(1, 0, 2)
local w: number = math.widen(math.narrow(1, 16), 16)

local a: vec3 = vec3.new(1, 2, 3)
local b: vec3 = vec3.new(4, 5, 6)
local m: number = vec3.distance(a, b) + vec3.distancesquared(a, b) + vec3.lengthsquared(a)
local e: vec3 = vec3.lerp(a, b, 0.5) + vec3.min(a, b) + vec3.max(a, b) + vec3.clamp(a, a, b) + vec3.abs(a) + vec3.sign(a)
local f: vec3 = vec3.perp(a) + vec3.muladd(a, 2, b) + vec3.mulsub(a, 2, b) + vec3.blend2(1, a, 2, b) + vec3.safescale(a)
local g: vec3 = vec3.subpos(a, b) + vec3.offsetpos(a, b) + vec3.lerpposition(a, b, 0.5)
local isunit: boolean = vec3.isnormalized(a) and vec3.isvalid(a)
local u1: vec3, l1: number = vec3.getlengthandnormalize(a)
local u2: vec3, l2: number = vec3.normalizewithlength(a)

local q: quat = quat.new(0, 0, 0.6, 0.8)
local r: quat = quat.normalize(q) * quat.invmul(q, q) * quat.negate(q) * quat.fromaxisangle(vec3.new(0, 0, 1), 1)
local r2: quat = quat.betweenunitvectors(vec3.new(1, 0, 0), vec3.new(0, 1, 0))
local axis: vec3, radians: number = quat.axisangle(q)
local angles: number = quat.dot(q, r) + quat.angle(q) + quat.twistangle(q) + quat.swingangle(q)
local qok: boolean = quat.isvalid(q) and quat.isnormalized(q)

local t: transform = transform.new(a, q)
local t2: transform = transform.invmul(t, t) * transform.makeworld(t) * transform.mulworld(t, t) * transform.invmulworld(t, t)
local t3: transform = transform.torelative(t, a)
local pt: vec3 = transform.transformworldpoint(t, a) + transform.invtransformworldpoint(t, a)
local tok: boolean = transform.isvalid(t)
local _ = {n, c, s, p, v, p2, v2, fits, w, m, e, f, g, isunit, u1, l1, u2, l2, r, r2, axis, radians, angles, qok, t2, t3, pt, tok}
)");

    LUAU_REQUIRE_NO_ERRORS(result);
}

// Layer 3c N3: vec2, mat3, plane, aabb and fix30 with exactly the components, operators and libraries of
// VM/src/moo_narrowlib.cpp (docs/DESIGN.md "Layer 3c as built").
TEST_CASE_FIXTURE(BuiltinsFixture, "moo_narrow_types_accept_everything_the_vm_implements")
{
    CheckResult result = check(R"(
--!strict
local v2: vec2 = vec2.new(1, 2)
local v2z: vec2 = vec2.new()
local v2n: number = v2.x + v2.y + v2.X + v2.Y

local a: vec3 = vec3.new(1, 2, 3)
local m: mat3 = mat3.new(a, a, a)
local i: mat3 = mat3.new()
local mm: mat3 = m + i - m * i + (-m) + m * 2 + 2 * m
local mv: vec3 = m * a
local cols: vec3 = m.cx + m.cy + m.cz
local inv: mat3 = mat3.transpose(m) + mat3.inverse(m) + mat3.inversewide(m) + mat3.inverset(m) + mat3.abs(m)
local sol: vec3 = mat3.solve(m, a) + mat3.solvewide(m, a)
local det: number = mat3.det(m)
local fq: mat3 = mat3.fromquat(quat.new())
local q: quat = quat.frommatrix(m)
local mok: boolean = mat3.isvalid(m)

local p: plane = plane.new(vec3.new(0, 1, 0), 2)
local p0: plane = plane.new(vec3.new(0, 1, 0))
local pn: vec3 = p.normal
local pd: number = p.offset
local pok: boolean = plane.isvalid(p)

local b: aabb = aabb.new(a, a)
local bp: aabb = aabb.frompoints({a, a}, 1)
local bp0: aabb = aabb.frompoints({a})
local corners: vec3 = b.lower + b.upper + aabb.center(b) + aabb.extents(b) + aabb.closestpoint(a, b)
local area: number = aabb.area(b)
local tests: boolean = aabb.contains(b, bp) and aabb.overlaps(b, bp) and aabb.isvalid(b)
local t: transform = transform.new()
local boxes: aabb = aabb.union(aabb.inflate(b, 1), aabb.transform(t, b))
local ce: aabb = aabb.fromcenterextents(t, a, a)

local f: fix30 = fix30.new(0.5)
local fn: number = fix30.tonumber(f)
local f2: fix30 = fix30.normalizecomponent(3, 4)
local fr: number = fix30.raw(f)
local f3: fix30 = fix30.fromraw(fr)

local _ = {v2, v2z, v2n, mm, mv, cols, inv, sol, det, fq, q, mok, p, p0, pn, pd, pok, corners, area, tests, boxes, ce, fn, f2, fr, f3}
)");

    LUAU_REQUIRE_NO_ERRORS(result);
}

// Each operation the VM refuses (tests/moo/vec3.test.cpp, quat.test.cpp and transform.test.cpp, the Refusals cases) is a type error here, and
// quat.slerp, which the VM refuses by name until fixed has a slerp, is absent.
TEST_CASE_FIXTURE(BuiltinsFixture, "moo_value_types_report_what_the_vm_refuses")
{
    struct Case
    {
        const char* source;
        const char* error;
    };
    const Case cases[] = {
        {"local _ = a + q", "Operator '+' could not be applied to operands of types vec3 and quat"},
        {"local _ = a * q", "Operator '*' could not be applied to operands of types vec3 and quat"},
        {"local _ = a / a", "Operator '/' could not be applied to operands of types vec3 and vec3"},
        {"local _ = a + 1", "Operator '+' could not be applied to operands of types vec3 and number"},
        {"local _ = q + q", "Operator '+' could not be applied to operands of types quat and quat"},
        {"local _ = q * 2", "Operator '*' could not be applied to operands of types quat and number"},
        {"local _ = t * q", "Operator '*' could not be applied to operands of types transform and quat"},
        {"local _ = t + t", "Operator '+' could not be applied to operands of types transform and transform"},
        {"local _ = quat.slerp(q, q, 0.5)", "Key 'slerp' not found"},
        {"local _ = a.w", "Key 'w' not found in external type 'vec3'"},
        {"local _ = t.x", "Key 'x' not found in external type 'transform'"},
        {"local _ = transform.new(q)", "Expected this to be 'vec3?', but got 'quat'"},
        {"local _ = transform.point(t, q)", "Expected this to be 'vec3', but got 'quat'"},
        {"local _ = quat.nlerp(q, q)", "Argument count mismatch"},
        {"local _ = vec3.dot(a, q)", "Expected this to be 'vec3', but got 'quat'"},
        // layer 3c
        {"local _ = vec3.lerp(a, a)", "Argument count mismatch"},
        {"local _ = vec3.muladd(a, a, a)", "Expected this to be 'number', but got 'vec3'"},
        {"local _ = quat.fromaxisangle(q, 1)", "Expected this to be 'vec3', but got 'quat'"},
        {"local _ = quat.invmul(q, a)", "Expected this to be 'quat', but got 'vec3'"},
        {"local _ = transform.torelative(t, q)", "Expected this to be 'vec3', but got 'quat'"},
        {"local _: vec3 = math.cossin(1)", "Expected this to be 'vec3', but got 'number'"},
        {"local _: quat = quat.axisangle(q)", "Expected this to be 'quat', but got 'vec3'"},
        // layer 3c N3: the narrow types
        {"local _ = vec2.new(1, 2) + vec2.new(1, 2)", "Operator '+' could not be applied to operands of types vec2 and vec2"},
        {"local _ = vec2.new(1, 2).z", "Key 'z' not found in external type 'vec2'"},
        {"local _ = mat3.new() + a", "Operator '+' could not be applied to operands of types mat3 and vec3"},
        {"local _ = mat3.new() * q", "Operator '*' could not be applied to operands of types mat3 and quat"},
        {"local _ = mat3.new() / mat3.new()", "Operator '/' could not be applied to operands of types mat3 and mat3"},
        {"local _ = mat3.new().cw", "Key 'cw' not found in external type 'mat3'"},
        {"local _ = mat3.new(1)", "Expected this to be 'vec3?', but got 'number'"},
        {"local _ = mat3.solve(mat3.new(), q)", "Expected this to be 'vec3', but got 'quat'"},
        {"local _ = quat.frommatrix(q)", "Expected this to be 'mat3', but got 'quat'"},
        {"local _ = plane.new(a, 1).x", "Key 'x' not found in external type 'plane'"},
        {"local _ = plane.new(a, 1) + plane.new(a, 1)", "Operator '+' could not be applied to operands of types plane and plane"},
        {"local _ = aabb.new(a)", "Argument count mismatch"},
        {"local _ = aabb.frompoints(a)", "Expected this to be '{vec3}', but got 'vec3'"},
        {"local _ = aabb.closestpoint(aabb.new(a, a), a)", "Expected this to be 'vec3', but got 'aabb'"},
        {"local _ = aabb.transform(aabb.new(a, a), t)", "Expected this to be 'transform', but got 'aabb'"},
        {"local _ = fix30.new(1) + 1", "Operator '+' could not be applied to operands of types fix30 and number"},
        {"local _: number = fix30.new(1)", "Expected this to be 'number', but got 'fix30'"},
        {"local _ = fix30.tonumber(1)", "Expected this to be 'fix30', but got 'number'"},
    };
    for (const Case& c : cases)
    {
        std::string source = std::string("--!strict\nlocal a, q, t = vec3.new(1, 2, 3), quat.new(), transform.new()\n") + c.source;
        CAPTURE(source);
        CheckResult result = check(source);
        REQUIRE(!result.errors.empty());
        std::string all;
        for (const TypeError& error : result.errors)
            all += toString(error) + "\n";
        CAPTURE(all);
        CHECK(all.find(c.error) != std::string::npos);
    }
}

TEST_SUITE_END();
