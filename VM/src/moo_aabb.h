// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: the aabb object, built as the vec3 object is (moo_vec3.h). One immutable GC leaf, tag LUA_TAABB: the common
// header and fixed's fixAABB, a lower and an upper bound fixVec3 (docs/DESIGN.md "Layer 3c as built").
#pragma once

#include "lobject.h"

#include "fixed/fixed.h"
#include "fixed/fixed_vec.h"

#include <stddef.h>

typedef struct MooAABB
{
    CommonHeader;
    // 5 bytes of padding: the header is gcseq (8) then tt, marked, memcat (3), and the payload is 8-aligned

    fixVec3 lower;
    fixVec3 upper;
} MooAABB;

// The payload is fixed's fixAABB (lowerBound, upperBound), component for component. Operations copy it out
// (moo_aabb_payload) and call fixed.
static_assert(sizeof(fixAABB) == 48, "fixAABB is two fixVec3");
static_assert(offsetof(MooAABB, lower) == sizeof(GCheader), "the payload starts right after the GC header");
static_assert(offsetof(MooAABB, upper) == offsetof(MooAABB, lower) + sizeof(fixVec3), "the upper bound follows the lower");
static_assert(sizeof(MooAABB) - offsetof(MooAABB, lower) == 48, "aabb payload is 48 bytes");
// totalbytes counts this size per aabb (docs/DETERMINISM.md 5), so it is pinned like every struct in moo_layout.cpp
static_assert(sizeof(MooAABB) == 64, "aabb object layout changed");

// longest tostring: "aabb(" + 2 x "vec3(" + 3 numbers of at most 34 characters + 2 ", " + ")" + ", " + ")"
#define MOO_AABB_TOSTR_MAX (5 + 2 * (5 + 3 * 34 + 2 * 2 + 1) + 2 + 1)

// a new aabb with a creation number; never written again (lobject.cpp)
LUAI_FUNC MooAABB* moo_aabb_new(lua_State* L, fixAABB a);

// 1 when the six raw components are equal (lobject.cpp)
LUAI_FUNC int moo_aabb_equal(const MooAABB* a, const MooAABB* b);

// "aabb(vec3(...), vec3(...))" into buf of at least MOO_AABB_TOSTR_MAX bytes; returns the end, not terminated
// (lobject.cpp)
LUAI_FUNC char* moo_aabb_tostring(char* buf, const MooAABB* a);

// the table hash of the raw components, before hashpow2, so equal aabb are one key (ltable.cpp)
LUAI_FUNC unsigned int moo_aabb_hash(const MooAABB* a);

inline fixAABB moo_aabb_payload(const MooAABB* a)
{
    return FIX_LITERAL(fixAABB){{a->lower.x, a->lower.y, a->lower.z}, {a->upper.x, a->upper.y, a->upper.z}};
}
