// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: the vec2 object, built as the vec3 object is (moo_vec3.h). One immutable GC leaf, tag LUA_TVEC2: the common
// header and fixed's fixVec2, two Q48.16 components (docs/DESIGN.md "Layer 3c as built").
#pragma once

#include "lobject.h"

#include "fixed/fixed.h"
#include "fixed/fixed_vec.h"

#include <stddef.h>

typedef struct MooVec2
{
    CommonHeader;
    // 5 bytes of padding: the header is gcseq (8) then tt, marked, memcat (3), and the payload is 8-aligned

    fixed_t x;
    fixed_t y;
} MooVec2;

// The payload is fixed's fixVec2, component for component. Operations copy it out (moo_vec2_payload) and call fixed;
// they never cast the object's address.
static_assert(sizeof(fixVec2) == 16, "fixVec2 is two fixed_t");
static_assert(offsetof(MooVec2, x) == sizeof(GCheader), "the payload starts right after the GC header");
static_assert(offsetof(MooVec2, y) == offsetof(MooVec2, x) + sizeof(fixed_t), "components are packed in order x, y");
static_assert(sizeof(MooVec2) - offsetof(MooVec2, x) == 16, "vec2 payload is 16 bytes");
// totalbytes counts this size per vec2 (docs/DETERMINISM.md 5), so it is pinned like every struct in moo_layout.cpp
static_assert(sizeof(MooVec2) == 32, "vec2 object layout changed");

// longest tostring: "vec2(" + 2 numbers of at most 34 characters (moo_numtostr) + ", " + ")"
#define MOO_VEC2_TOSTR_MAX (5 + 2 * 34 + 2 + 1)

// a new vec2 with a creation number; never written again (lobject.cpp)
LUAI_FUNC MooVec2* moo_vec2_new(lua_State* L, fixVec2 v);

// 1 when the raw components are equal: a vec2 is a value, never compared by address or creation number (lobject.cpp)
LUAI_FUNC int moo_vec2_equal(const MooVec2* a, const MooVec2* b);

// "vec2(x, y)" into buf of at least MOO_VEC2_TOSTR_MAX bytes; returns the end, not terminated (lobject.cpp)
LUAI_FUNC char* moo_vec2_tostring(char* buf, const MooVec2* v);

// the table hash of the raw components, before hashpow2, so equal vec2 are one key (ltable.cpp)
LUAI_FUNC unsigned int moo_vec2_hash(const MooVec2* v);

inline fixVec2 moo_vec2_payload(const MooVec2* v)
{
    return FIX_LITERAL(fixVec2){v->x, v->y};
}
