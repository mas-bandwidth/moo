// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: the vec3 object of layer 3b (docs/DESIGN.md "vec3, quat and transform (layer 3b)", option (a) of
// "Representations costed"). The decisions and their reasons are in docs/layer3b/moo-v2-l3b-vec3-object.md.
//
// A vec3 is one immutable GC object: the common header (with its creation number, docs/DETERMINISM.md 1)
// and three Q48.16 components. It holds no reference to another object, so the GC treats it as a leaf, as
// it treats a string. Its tag is LUA_TVEC3 (lua.h, note decision 1). Nothing here creates one yet: card
// moo-v2-l3b-vec3-object-gc defines the functions declared below and card moo-v2-l3b-vec3-object-key defines the hash.
#pragma once

#include "lobject.h"

#include "fixed/fixed.h"
#include "fixed/fixed_vec.h"

#include <stddef.h>

typedef struct MooVec3
{
    CommonHeader;
    // 5 bytes of padding: the header is gcseq (8) then tt, marked, memcat (3), and the payload is 8-aligned

    fixed_t x;
    fixed_t y;
    fixed_t z;
} MooVec3;

// The payload is fixed's fixVec3, component for component (DESIGN.md table: 24 bytes). An operation copies the
// components into a fixVec3 by value and calls fixed's function on it; it never casts the object's address.
static_assert(sizeof(fixVec3) == 24, "fixVec3 is three fixed_t");
static_assert(offsetof(MooVec3, x) == sizeof(GCheader), "the payload starts right after the GC header");
static_assert(offsetof(MooVec3, y) == offsetof(MooVec3, x) + sizeof(fixed_t), "components are packed in order x, y, z");
static_assert(offsetof(MooVec3, z) == offsetof(MooVec3, y) + sizeof(fixed_t), "components are packed in order x, y, z");
static_assert(sizeof(MooVec3) - offsetof(MooVec3, x) == 24, "vec3 payload is 24 bytes (docs/DESIGN.md, vec3, quat and transform)");
// totalbytes counts this size per vec3 (docs/DETERMINISM.md 5), so it is pinned like every struct in moo_layout.cpp
static_assert(sizeof(MooVec3) == 40, "vec3 object layout changed");

// Longest tostring: "vec3(" + 3 numbers of at most 34 characters (moo_numtostr) + 2 ", " + ")".
#define MOO_VEC3_TOSTR_MAX (5 + 3 * 34 + 2 * 2 + 1)

// Allocates a vec3 (luaM_newgcofixed, then luaC_init, which takes the creation number) and stores the three
// components; the object is never written again. In lobject.cpp, card moo-v2-l3b-vec3-object-gc; note, "Card gc".
LUAI_FUNC MooVec3* moo_vec3_new(lua_State* L, fixed_t x, fixed_t y, fixed_t z);

// 1 when the three raw components are equal, else 0: equality is by value, never by address or creation number
// (note, decision 3). In lobject.cpp, card moo-v2-l3b-vec3-object-gc.
LUAI_FUNC int moo_vec3_equal(const MooVec3* a, const MooVec3* b);

// Writes "vec3(x, y, z)", each component through moo_numtostr (the shortest decimal that reads back exactly,
// docs/DESIGN.md "Number formatting"), into buf of at least MOO_VEC3_TOSTR_MAX bytes; returns the end, not
// terminated, as moo_numtostr does (note, decision 5). In lobject.cpp, card moo-v2-l3b-vec3-object-gc.
LUAI_FUNC char* moo_vec3_tostring(char* buf, const MooVec3* v);

// The 32-bit table hash of the three raw components, before hashpow2: each component through hashint's
// MurmurHash64B finalizer, combined with the spatial-hash primes of hashvec, so equal vec3 are one key (note,
// decision 4). In ltable.cpp, card moo-v2-l3b-vec3-object-key.
LUAI_FUNC unsigned int moo_vec3_hash(const MooVec3* v);
