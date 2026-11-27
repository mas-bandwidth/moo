// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: the plane object, built as the vec3 object is (moo_vec3.h). One immutable GC leaf, tag LUA_TPLANE: the common
// header and fixed's fixPlane, a unit normal fixVec3 and an offset along it (docs/DESIGN.md "Layer 3c as built").
#pragma once

#include "lobject.h"

#include "fixed/fixed.h"
#include "fixed/fixed_vec.h"

#include <stddef.h>

typedef struct MooPlane
{
    CommonHeader;
    // 5 bytes of padding: the header is gcseq (8) then tt, marked, memcat (3), and the payload is 8-aligned

    fixVec3 normal;
    fixed_t offset;
} MooPlane;

// The payload is fixed's fixPlane, component for component. Operations copy it out (moo_plane_payload) and call fixed.
static_assert(sizeof(fixPlane) == 32, "fixPlane is a fixVec3 and a fixed_t");
static_assert(offsetof(MooPlane, normal) == sizeof(GCheader), "the payload starts right after the GC header");
static_assert(offsetof(MooPlane, offset) == offsetof(MooPlane, normal) + sizeof(fixVec3), "the offset follows the normal");
static_assert(sizeof(MooPlane) - offsetof(MooPlane, normal) == 32, "plane payload is 32 bytes");
// totalbytes counts this size per plane (docs/DETERMINISM.md 5), so it is pinned like every struct in moo_layout.cpp
static_assert(sizeof(MooPlane) == 48, "plane object layout changed");

// longest tostring: "plane(vec3(" + 3 numbers of at most 34 characters + 2 ", " + "), " + 1 number + ")"
#define MOO_PLANE_TOSTR_MAX (11 + 3 * 34 + 2 * 2 + 3 + 34 + 1)

// a new plane with a creation number; never written again (lobject.cpp)
LUAI_FUNC MooPlane* moo_plane_new(lua_State* L, fixPlane p);

// 1 when the four raw components are equal (lobject.cpp)
LUAI_FUNC int moo_plane_equal(const MooPlane* a, const MooPlane* b);

// "plane(vec3(nx, ny, nz), offset)" into buf of at least MOO_PLANE_TOSTR_MAX bytes; returns the end, not terminated
// (lobject.cpp)
LUAI_FUNC char* moo_plane_tostring(char* buf, const MooPlane* p);

// the table hash of the raw components, before hashpow2, so equal planes are one key (ltable.cpp)
LUAI_FUNC unsigned int moo_plane_hash(const MooPlane* p);

inline fixPlane moo_plane_payload(const MooPlane* p)
{
    return FIX_LITERAL(fixPlane){{p->normal.x, p->normal.y, p->normal.z}, p->offset};
}
