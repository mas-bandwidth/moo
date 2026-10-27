// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: the quat object, built as the vec3 object is (moo_vec3.h). One immutable GC leaf, tag LUA_TQUAT: the common
// header and fixed's fixQuat, a fixVec3 and a scalar s.
#pragma once

#include "lobject.h"

#include "fixed/fixed.h"
#include "fixed/fixed_vec.h"

#include <stddef.h>

typedef struct MooQuat
{
    CommonHeader;
    // 5 bytes of padding: the header is gcseq (8) then tt, marked, memcat (3), and the payload is 8-aligned

    fixVec3 v;
    fixed_t s;
} MooQuat;

// The payload is fixed's fixQuat, component for component. Operations copy it out (moo_quat_payload) and call fixed.
static_assert(sizeof(fixQuat) == 32, "fixQuat is a fixVec3 and a fixed_t");
static_assert(offsetof(MooQuat, v) == sizeof(GCheader), "the payload starts right after the GC header");
static_assert(offsetof(MooQuat, v.y) == offsetof(MooQuat, v.x) + sizeof(fixed_t), "components are packed in order x, y, z");
static_assert(offsetof(MooQuat, v.z) == offsetof(MooQuat, v.y) + sizeof(fixed_t), "components are packed in order x, y, z");
static_assert(offsetof(MooQuat, s) == offsetof(MooQuat, v.z) + sizeof(fixed_t), "the scalar follows the vector");
static_assert(sizeof(MooQuat) - offsetof(MooQuat, v) == 32, "quat payload is 32 bytes");
// totalbytes counts this size per quat (docs/DETERMINISM.md 5), so it is pinned like every struct in moo_layout.cpp
static_assert(sizeof(MooQuat) == 48, "quat object layout changed");

// longest tostring: "quat(" + 4 numbers of at most 34 characters (moo_numtostr) + 3 ", " + ")"
#define MOO_QUAT_TOSTR_MAX (5 + 4 * 34 + 3 * 2 + 1)

// a new quat with a creation number; never written again (lobject.cpp)
LUAI_FUNC MooQuat* moo_quat_new(lua_State* L, fixed_t x, fixed_t y, fixed_t z, fixed_t s);

// 1 when the raw components are equal (lobject.cpp)
LUAI_FUNC int moo_quat_equal(const MooQuat* a, const MooQuat* b);

// "quat(x, y, z, s)" into buf of at least MOO_QUAT_TOSTR_MAX bytes; returns the end, not terminated (lobject.cpp)
LUAI_FUNC char* moo_quat_tostring(char* buf, const MooQuat* q);

// the table hash of the raw components, before hashpow2, so equal quat are one key (ltable.cpp)
LUAI_FUNC unsigned int moo_quat_hash(const MooQuat* q);

// the argument at idx as a fixQuat, or the "quat" type error (moo_quatlib.cpp)
LUAI_FUNC fixQuat moo_quat_check(lua_State* L, int idx);

// pushes a new quat (moo_quatlib.cpp)
LUAI_FUNC void moo_quat_push(lua_State* L, fixQuat q);

inline fixQuat moo_quat_payload(const MooQuat* q)
{
    return FIX_LITERAL(fixQuat){{q->v.x, q->v.y, q->v.z}, q->s};
}
