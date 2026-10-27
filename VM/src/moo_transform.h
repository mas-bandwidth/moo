// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: the transform object, built as the vec3 object is (moo_vec3.h). One immutable GC leaf, tag LUA_TTRANSFORM:
// the common header and fixed's fixTransform, a position fixVec3 and a rotation fixQuat
// (docs/layer3b/moo-v2-l3b-transform.md).
#pragma once

#include "lobject.h"

#include "fixed/fixed.h"
#include "fixed/fixed_vec.h"

#include <stddef.h>

typedef struct MooTransform
{
    CommonHeader;
    // 5 bytes of padding: the header is gcseq (8) then tt, marked, memcat (3), and the payload is 8-aligned

    fixVec3 p;
    fixQuat q;
} MooTransform;

// The payload is fixed's fixTransform (fixed3d's b3Transform), component for component. Operations copy it out
// (moo_transform_payload) and call fixed.
static_assert(sizeof(fixTransform) == 56, "fixTransform is a fixVec3 and a fixQuat");
static_assert(offsetof(MooTransform, p) == sizeof(GCheader), "the payload starts right after the GC header");
static_assert(offsetof(MooTransform, q) == offsetof(MooTransform, p) + sizeof(fixVec3), "the rotation follows the position");
static_assert(offsetof(MooTransform, q.s) == offsetof(MooTransform, q.v) + sizeof(fixVec3), "the scalar follows the vector");
static_assert(sizeof(MooTransform) - offsetof(MooTransform, p) == 56, "transform payload is 56 bytes");
// totalbytes counts this size per transform (docs/DETERMINISM.md 5), so it is pinned like every struct in moo_layout.cpp
static_assert(sizeof(MooTransform) == 72, "transform object layout changed");

// longest tostring: "transform(vec3(" + 3 numbers + "), quat(" + 4 numbers + "))", each number at most 34
// characters (moo_numtostr), with ", " between numbers
#define MOO_TRANSFORM_TOSTR_MAX (15 + 3 * 34 + 2 * 2 + 8 + 4 * 34 + 3 * 2 + 2)

// a new transform with a creation number; never written again (lobject.cpp)
LUAI_FUNC MooTransform* moo_transform_new(lua_State* L, fixTransform t);

// 1 when the raw components are equal (lobject.cpp)
LUAI_FUNC int moo_transform_equal(const MooTransform* a, const MooTransform* b);

// "transform(vec3(px, py, pz), quat(qx, qy, qz, qs))" into buf of at least MOO_TRANSFORM_TOSTR_MAX bytes; returns
// the end, not terminated (lobject.cpp)
LUAI_FUNC char* moo_transform_tostring(char* buf, const MooTransform* t);

// the table hash of the raw components, before hashpow2, so equal transforms are one key (ltable.cpp)
LUAI_FUNC unsigned int moo_transform_hash(const MooTransform* t);

inline fixTransform moo_transform_payload(const MooTransform* t)
{
    return FIX_LITERAL(fixTransform){{t->p.x, t->p.y, t->p.z}, {{t->q.v.x, t->q.v.y, t->q.v.z}, t->q.s}};
}
