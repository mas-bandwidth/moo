// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: the quat object of layer 3b (docs/DESIGN.md "vec3, quat and transform (layer 3b)", option (a) of
// "Representations costed"). It copies the vec3 object's decisions (docs/layer3b/moo-v2-l3b-vec3-object.md).
//
// A quat is one immutable GC object: the common header (with its creation number, docs/DETERMINISM.md 1) and
// fixed's fixQuat, a fixVec3 and a scalar, 32 bytes of payload. It holds no reference to another object, so the
// GC treats it as a leaf, as it treats a string. Its tag is LUA_TQUAT (lua.h, note decision 1). Operations are
// card moo-v2-l3b-quat's; this card makes the object kind only.
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

// The payload is fixed's fixQuat, component for component (DESIGN.md table: 32 bytes). An operation copies the
// components into a fixQuat by value and calls fixed's function on it; it never casts the object's address.
static_assert(sizeof(fixQuat) == 32, "fixQuat is a fixVec3 and a fixed_t");
static_assert(offsetof(MooQuat, v) == sizeof(GCheader), "the payload starts right after the GC header");
static_assert(offsetof(MooQuat, v.y) == offsetof(MooQuat, v.x) + sizeof(fixed_t), "components are packed in order x, y, z");
static_assert(offsetof(MooQuat, v.z) == offsetof(MooQuat, v.y) + sizeof(fixed_t), "components are packed in order x, y, z");
static_assert(offsetof(MooQuat, s) == offsetof(MooQuat, v.z) + sizeof(fixed_t), "the scalar follows the vector");
static_assert(sizeof(MooQuat) - offsetof(MooQuat, v) == 32, "quat payload is 32 bytes (docs/DESIGN.md, vec3, quat and transform)");
// totalbytes counts this size per quat (docs/DETERMINISM.md 5), so it is pinned like every struct in moo_layout.cpp
static_assert(sizeof(MooQuat) == 48, "quat object layout changed");

// Longest tostring: "quat(" + 4 numbers of at most 34 characters (moo_numtostr) + 3 ", " + ")".
#define MOO_QUAT_TOSTR_MAX (5 + 4 * 34 + 3 * 2 + 1)

// Allocates a quat (luaM_newgcofixed, then luaC_init, which takes the creation number) and stores the four
// components; the object is never written again. In lobject.cpp, card moo-v2-l3b-quat-object.
LUAI_FUNC MooQuat* moo_quat_new(lua_State* L, fixed_t x, fixed_t y, fixed_t z, fixed_t s);

// 1 when the four raw components are equal, else 0: equality is by value, never by address or creation number
// (vec3 note, decision 3). In lobject.cpp, card moo-v2-l3b-quat-object.
LUAI_FUNC int moo_quat_equal(const MooQuat* a, const MooQuat* b);

// Writes "quat(x, y, z, s)", each component through moo_numtostr (the shortest decimal that reads back exactly,
// docs/DESIGN.md "Number formatting"), into buf of at least MOO_QUAT_TOSTR_MAX bytes; returns the end, not
// terminated, as moo_numtostr does (vec3 note, decision 5). In lobject.cpp, card moo-v2-l3b-quat-object.
LUAI_FUNC char* moo_quat_tostring(char* buf, const MooQuat* q);

// The 32-bit table hash of the four raw components, before hashpow2: each component through hashint's
// MurmurHash64B finalizer, combined with hashvec's spatial-hash primes, so equal quat are one key (vec3 note,
// decision 4). In ltable.cpp, card moo-v2-l3b-quat-object.
LUAI_FUNC unsigned int moo_quat_hash(const MooQuat* q);
