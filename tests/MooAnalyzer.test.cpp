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
