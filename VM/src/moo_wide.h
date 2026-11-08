// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: the layer 3d values, built as the vec3 object is (moo_vec3.h): five immutable GC leaves after the layer 3c tags
// (docs/DESIGN.md "Layer 3d as built").
//
//   wide                LUA_TWIDE                MooFixedWide                 fixed's fixedWide_t, Q112.16
//   poswide             LUA_TPOSWIDE             MooPosWide              fixPosWide, three Q112.16 coordinates
//   worldtransformwide  LUA_TWORLDTRANSFORMWIDE  MooWorldTransformWide   fixWorldTransformWide, a poswide and a quat
//   aabbwide            LUA_TAABBWIDE            MooAABBWide             fixAABBWide, two poswide bounds
//   time                LUA_TTIME                MooTime                 fixTime, Q32.32 seconds
//
// A Q112.16 value is stored as two 64-bit limbs (lo, hi), never as fixed's fixInt128: that type is a native __int128
// with 16-byte alignment on some targets and a struct on others, and GC blocks are 8-aligned (lmem.cpp kBlockHeader).
// Operations copy the limbs out (the *_payload functions), call fixed and store the limbs of the result; they never
// cast an object's address.
#pragma once

#include "lobject.h"

#include "fixed/fixed.h"
#include "fixed/fixed_vec.h"
#include "fixed/fixed_wide.h"
#include "fixed/fixed_time.h"

#include <stddef.h>
#include <stdint.h>

// a Q112.16 coordinate as stored in an object: two's complement, low limb first
typedef struct MooWideLimbs
{
    uint64_t lo;
    uint64_t hi;
} MooWideLimbs;

typedef struct MooFixedWide
{
    CommonHeader;
    // 5 bytes of padding: the header is gcseq (8) then tt, marked, memcat (3), and the payload is 8-aligned
    MooWideLimbs w;
} MooFixedWide;

typedef struct MooPosWide
{
    CommonHeader;
    MooWideLimbs x;
    MooWideLimbs y;
    MooWideLimbs z;
} MooPosWide;

typedef struct MooWorldTransformWide
{
    CommonHeader;
    MooWideLimbs px;
    MooWideLimbs py;
    MooWideLimbs pz;
    fixQuat q;
} MooWorldTransformWide;

typedef struct MooAABBWide
{
    CommonHeader;
    MooWideLimbs lx;
    MooWideLimbs ly;
    MooWideLimbs lz;
    MooWideLimbs ux;
    MooWideLimbs uy;
    MooWideLimbs uz;
} MooAABBWide;

typedef struct MooTime
{
    CommonHeader;
    // the payload is 8-aligned at offset 16, as fix30's is (moo_fix30.h): every GC object is at least 24 bytes
    fixTime t;
} MooTime;

static_assert(sizeof(MooWideLimbs) == 16 && alignof(MooWideLimbs) == 8, "a wide coordinate is two 8-aligned limbs");
static_assert(sizeof(fixQuat) == 32, "fixQuat is four fixed_t");
static_assert(sizeof(fixTime) == 8, "fixTime is one int64_t");
static_assert(offsetof(MooFixedWide, w) == sizeof(GCheader), "the payload starts right after the GC header");
static_assert(offsetof(MooPosWide, x) == sizeof(GCheader), "the payload starts right after the GC header");
static_assert(offsetof(MooWorldTransformWide, px) == sizeof(GCheader), "the payload starts right after the GC header");
static_assert(offsetof(MooWorldTransformWide, q) == sizeof(GCheader) + 48, "the rotation follows the position");
static_assert(offsetof(MooAABBWide, lx) == sizeof(GCheader), "the payload starts right after the GC header");
static_assert(offsetof(MooTime, t) == sizeof(GCheader), "the payload starts right after the GC header");
// totalbytes counts these sizes per value (docs/DETERMINISM.md 5), so they are pinned like every struct in
// moo_layout.cpp
static_assert(sizeof(MooFixedWide) == 32, "wide object layout changed");
static_assert(sizeof(MooPosWide) == 64, "poswide object layout changed");
static_assert(sizeof(MooWorldTransformWide) == 96, "worldtransformwide object layout changed");
static_assert(sizeof(MooAABBWide) == 112, "aabbwide object layout changed");
static_assert(sizeof(MooTime) == 24, "time object layout changed");

// longest tostring of each (moo_fixtext.h MOO_FIXTOSTR_MAX per coordinate, plus its suffix): "1.5w", "0.25s",
// "poswide(xw, yw, zw)", "worldtransformwide(poswide(...), quat(...))", "aabbwide(poswide(...), poswide(...))"
#define MOO_WIDE_TOSTR_MAX (52 + 1)
#define MOO_TIME_TOSTR_MAX (52 + 1)
#define MOO_POSWIDE_TOSTR_MAX (8 + 3 * MOO_WIDE_TOSTR_MAX + 2 * 2 + 1)
#define MOO_WORLDTRANSFORMWIDE_TOSTR_MAX (19 + MOO_POSWIDE_TOSTR_MAX + 2 + 5 + 4 * 34 + 3 * 2 + 1 + 1)
#define MOO_AABBWIDE_TOSTR_MAX (9 + 2 * MOO_POSWIDE_TOSTR_MAX + 2 + 1)

inline MooWideLimbs moo_wide_limbs(fixedWide_t a)
{
    MooWideLimbs l = {fixInt128Lo(a), fixInt128Hi(a)};
    return l;
}

inline fixedWide_t moo_wide_value(MooWideLimbs l)
{
    return fixInt128Make(l.hi, l.lo);
}

inline int moo_wide_limbs_equal(MooWideLimbs a, MooWideLimbs b)
{
    return a.lo == b.lo && a.hi == b.hi;
}

// new values with a creation number; never written again (lobject.cpp)
LUAI_FUNC MooFixedWide* moo_wide_new(lua_State* L, fixedWide_t a);
LUAI_FUNC MooPosWide* moo_poswide_new(lua_State* L, fixPosWide p);
LUAI_FUNC MooWorldTransformWide* moo_worldtransformwide_new(lua_State* L, fixWorldTransformWide t);
LUAI_FUNC MooAABBWide* moo_aabbwide_new(lua_State* L, fixAABBWide b);
LUAI_FUNC MooTime* moo_time_new(lua_State* L, fixTime t);

// 1 when every raw limb is equal: a value is its bits, never compared by address or creation number (lobject.cpp)
LUAI_FUNC int moo_wide_equal(const MooFixedWide* a, const MooFixedWide* b);
LUAI_FUNC int moo_poswide_equal(const MooPosWide* a, const MooPosWide* b);
LUAI_FUNC int moo_worldtransformwide_equal(const MooWorldTransformWide* a, const MooWorldTransformWide* b);
LUAI_FUNC int moo_aabbwide_equal(const MooAABBWide* a, const MooAABBWide* b);
LUAI_FUNC int moo_time_equal(const MooTime* a, const MooTime* b);

// the literal-like text, into buf of at least the type's *_TOSTR_MAX bytes; returns the end, not terminated
// (lobject.cpp). A wide prints as its literal (1.5w), a time as its literal (0.25s), so the text reads back.
LUAI_FUNC char* moo_wide_tostring(char* buf, const MooFixedWide* a);
LUAI_FUNC char* moo_poswide_tostring(char* buf, const MooPosWide* p);
LUAI_FUNC char* moo_worldtransformwide_tostring(char* buf, const MooWorldTransformWide* t);
LUAI_FUNC char* moo_aabbwide_tostring(char* buf, const MooAABBWide* b);
LUAI_FUNC char* moo_time_tostring(char* buf, const MooTime* t);

// the table hashes of the raw limbs, before hashpow2, so equal values are one key (ltable.cpp)
LUAI_FUNC unsigned int moo_wide_hash(const MooFixedWide* a);
LUAI_FUNC unsigned int moo_poswide_hash(const MooPosWide* p);
LUAI_FUNC unsigned int moo_worldtransformwide_hash(const MooWorldTransformWide* t);
LUAI_FUNC unsigned int moo_aabbwide_hash(const MooAABBWide* b);
LUAI_FUNC unsigned int moo_time_hash(const MooTime* t);

inline fixedWide_t moo_wide_payload(const MooFixedWide* a)
{
    return moo_wide_value(a->w);
}

inline fixPosWide moo_poswide_payload(const MooPosWide* p)
{
    fixPosWide r;
    r.x = moo_wide_value(p->x);
    r.y = moo_wide_value(p->y);
    r.z = moo_wide_value(p->z);
    return r;
}

inline fixWorldTransformWide moo_worldtransformwide_payload(const MooWorldTransformWide* t)
{
    fixWorldTransformWide r;
    r.p.x = moo_wide_value(t->px);
    r.p.y = moo_wide_value(t->py);
    r.p.z = moo_wide_value(t->pz);
    r.q = t->q;
    return r;
}

inline fixAABBWide moo_aabbwide_payload(const MooAABBWide* b)
{
    fixAABBWide r;
    r.lowerBound.x = moo_wide_value(b->lx);
    r.lowerBound.y = moo_wide_value(b->ly);
    r.lowerBound.z = moo_wide_value(b->lz);
    r.upperBound.x = moo_wide_value(b->ux);
    r.upperBound.y = moo_wide_value(b->uy);
    r.upperBound.z = moo_wide_value(b->uz);
    return r;
}

inline fixTime moo_time_payload(const MooTime* t)
{
    return t->t;
}
