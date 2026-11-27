// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: the mat3 object, built as the vec3 object is (moo_vec3.h). One immutable GC leaf, tag LUA_TMAT3: the common
// header and fixed's fixMatrix3, three column vectors cx, cy, cz (docs/DESIGN.md "Layer 3c as built").
#pragma once

#include "lobject.h"

#include "fixed/fixed.h"
#include "fixed/fixed_vec.h"

#include <stddef.h>

typedef struct MooMat3
{
    CommonHeader;
    // 5 bytes of padding: the header is gcseq (8) then tt, marked, memcat (3), and the payload is 8-aligned

    fixVec3 cx;
    fixVec3 cy;
    fixVec3 cz;
} MooMat3;

// The payload is fixed's fixMatrix3, column for column. Operations copy it out (moo_mat3_payload) and call fixed.
static_assert(sizeof(fixMatrix3) == 72, "fixMatrix3 is three fixVec3");
static_assert(offsetof(MooMat3, cx) == sizeof(GCheader), "the payload starts right after the GC header");
static_assert(offsetof(MooMat3, cy) == offsetof(MooMat3, cx) + sizeof(fixVec3), "columns are packed in order cx, cy, cz");
static_assert(offsetof(MooMat3, cz) == offsetof(MooMat3, cy) + sizeof(fixVec3), "columns are packed in order cx, cy, cz");
static_assert(sizeof(MooMat3) - offsetof(MooMat3, cx) == 72, "mat3 payload is 72 bytes");
// totalbytes counts this size per mat3 (docs/DETERMINISM.md 5), so it is pinned like every struct in moo_layout.cpp
static_assert(sizeof(MooMat3) == 88, "mat3 object layout changed");

// longest tostring: "mat3(" + 3 x "vec3(" + 3 numbers of at most 34 characters + 2 ", " + ")" + 2 ", " + ")"
#define MOO_MAT3_TOSTR_MAX (5 + 3 * (5 + 3 * 34 + 2 * 2 + 1) + 2 * 2 + 1)

// a new mat3 with a creation number; never written again (lobject.cpp)
LUAI_FUNC MooMat3* moo_mat3_new(lua_State* L, fixMatrix3 m);

// 1 when the nine raw components are equal (lobject.cpp)
LUAI_FUNC int moo_mat3_equal(const MooMat3* a, const MooMat3* b);

// "mat3(vec3(...), vec3(...), vec3(...))" into buf of at least MOO_MAT3_TOSTR_MAX bytes; returns the end, not
// terminated (lobject.cpp)
LUAI_FUNC char* moo_mat3_tostring(char* buf, const MooMat3* m);

// the table hash of the raw components, before hashpow2, so equal mat3 are one key (ltable.cpp)
LUAI_FUNC unsigned int moo_mat3_hash(const MooMat3* m);

inline fixMatrix3 moo_mat3_payload(const MooMat3* m)
{
    return FIX_LITERAL(fixMatrix3){{m->cx.x, m->cx.y, m->cx.z}, {m->cy.x, m->cy.y, m->cy.z}, {m->cz.x, m->cz.y, m->cz.z}};
}
