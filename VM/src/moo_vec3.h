// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: the vec3 object. One immutable GC object, tag LUA_TVEC3: the common header and three Q48.16 components. It
// references nothing, so the GC treats it as a leaf, like a string. The decisions and their reasons are in
// docs/layer3b/moo-v2-l3b-vec3-object.md.
#pragma once

#include "lobject.h"

#include "fixed/fixed.h"
#include "fixed/fixed_vec.h"

#include <stddef.h>

// vec3, quat and transform arithmetic saturates as number does only with fixed's FIX_SATURATE
#if !defined(FIX_SATURATE)
#error "moo builds fixed with FIX_SATURATE"
#endif

typedef struct MooVec3
{
    CommonHeader;
    // 5 bytes of padding: the header is gcseq (8) then tt, marked, memcat (3), and the payload is 8-aligned

    fixed_t x;
    fixed_t y;
    fixed_t z;
} MooVec3;

// The payload is fixed's fixVec3, component for component. Operations copy it out (moo_vec3_payload) and call fixed;
// they never cast the object's address.
static_assert(sizeof(fixVec3) == 24, "fixVec3 is three fixed_t");
static_assert(offsetof(MooVec3, x) == sizeof(GCheader), "the payload starts right after the GC header");
static_assert(offsetof(MooVec3, y) == offsetof(MooVec3, x) + sizeof(fixed_t), "components are packed in order x, y, z");
static_assert(offsetof(MooVec3, z) == offsetof(MooVec3, y) + sizeof(fixed_t), "components are packed in order x, y, z");
static_assert(sizeof(MooVec3) - offsetof(MooVec3, x) == 24, "vec3 payload is 24 bytes");
// totalbytes counts this size per vec3 (docs/DETERMINISM.md 5), so it is pinned like every struct in moo_layout.cpp
static_assert(sizeof(MooVec3) == 40, "vec3 object layout changed");

// longest tostring: "vec3(" + 3 numbers of at most 34 characters (moo_numtostr) + 2 ", " + ")"
#define MOO_VEC3_TOSTR_MAX (5 + 3 * 34 + 2 * 2 + 1)

// a new vec3 with a creation number; never written again (lobject.cpp)
LUAI_FUNC MooVec3* moo_vec3_new(lua_State* L, fixed_t x, fixed_t y, fixed_t z);

// 1 when the raw components are equal: a vec3 is a value, never compared by address or creation number (lobject.cpp)
LUAI_FUNC int moo_vec3_equal(const MooVec3* a, const MooVec3* b);

// "vec3(x, y, z)" into buf of at least MOO_VEC3_TOSTR_MAX bytes; returns the end, not terminated (lobject.cpp)
LUAI_FUNC char* moo_vec3_tostring(char* buf, const MooVec3* v);

// the table hash of the raw components, before hashpow2, so equal vec3 are one key (ltable.cpp)
LUAI_FUNC unsigned int moo_vec3_hash(const MooVec3* v);

// the argument at idx as a fixVec3, or the "vec3" type error (moo_vec3lib.cpp)
LUAI_FUNC fixVec3 moo_vec3_check(lua_State* L, int idx);

// pushes a new vec3 (moo_vec3lib.cpp)
LUAI_FUNC void moo_vec3_push(lua_State* L, fixVec3 v);

inline fixVec3 moo_vec3_payload(const MooVec3* v)
{
    return FIX_LITERAL(fixVec3){v->x, v->y, v->z};
}
