// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: the fix30 object, built as the vec3 object is (moo_vec3.h). One immutable GC leaf, tag LUA_TFIX30: the common
// header and fixed's fixed30_t, one Q2.30 component in 32 bits (docs/DESIGN.md "Layer 3c as built"). It is a GC
// object, not a TValue payload, so that no fix30 is ever read as a number: a value is its tag and its bits.
#pragma once

#include "lobject.h"

#include "fixed/fixed.h"

#include <stddef.h>

typedef struct MooFix30
{
    CommonHeader;
    // 5 bytes of padding: the header is gcseq (8) then tt, marked, memcat (3). The payload is 8-aligned at offset 16,
    // not in the header's tail padding: a free block keeps its free-list link right after the header (lmem.cpp
    // kGCOLinkOffset), so every GC object is at least 24 bytes

    alignas(8) int32_t raw;
} MooFix30;

// The payload is fixed's fixed30_t. Operations copy it out (moo_fix30_payload) and call fixed.
static_assert(sizeof(fixed30_t) == 4, "fixed30_t is one int32_t");
static_assert(offsetof(MooFix30, raw) == sizeof(GCheader), "the payload starts right after the GC header");
// totalbytes counts this size per fix30 (docs/DETERMINISM.md 5), so it is pinned like every struct in moo_layout.cpp
static_assert(sizeof(MooFix30) == 24, "fix30 object layout changed");

// longest tostring: "fix30(" + the Q2.30 value in decimal: a sign, one integer digit, a point and 30 fraction digits
// (1 / 2^30 has 30 decimal places), + ")"
#define MOO_FIX30_TOSTR_MAX (6 + 1 + 1 + 1 + 30 + 1)

// a new fix30 with a creation number; never written again (lobject.cpp)
LUAI_FUNC MooFix30* moo_fix30_new(lua_State* L, fixed30_t x);

// 1 when the raw components are equal (lobject.cpp)
LUAI_FUNC int moo_fix30_equal(const MooFix30* a, const MooFix30* b);

// "fix30(x)" with the exact Q2.30 value in decimal, every nonzero fraction digit, into buf of at least
// MOO_FIX30_TOSTR_MAX bytes; returns the end, not terminated (lobject.cpp)
LUAI_FUNC char* moo_fix30_tostring(char* buf, const MooFix30* x);

// the table hash of the raw value, before hashpow2, so equal fix30 are one key (ltable.cpp)
LUAI_FUNC unsigned int moo_fix30_hash(const MooFix30* x);

inline fixed30_t moo_fix30_payload(const MooFix30* x)
{
    fixed30_t r = {x->raw};
    return r;
}
