// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
#pragma once

#include "lobject.h"
#include "lgc.h"

#include <stddef.h>
#include <stdint.h>

// moo: immutable 3-component Q48.16 fixed-point vector GC object (docs/layer3b/moo-v2-l3b-vec3-object.md).
// CommonHeader is 16 bytes on 64-bit moo (8-byte gcseq + 3 bytes tt/marked/memcat + 5 bytes padding),
// followed by three 8-byte signed Q48.16 components x, y, z (24 bytes) = 40 bytes total.
typedef struct moo_Vec3
{
    CommonHeader;

    int64_t x;
    int64_t y;
    int64_t z;
} moo_Vec3;

static_assert(sizeof(GCheader) == 16, "moo_Vec3 header relies on 16-byte GCheader");
static_assert(sizeof(moo_Vec3) == 40, "moo_Vec3 layout changed (expected 16-byte GCheader + 24-byte Q48.16 payload)");
static_assert(alignof(moo_Vec3) == 8, "moo_Vec3 alignment changed");
static_assert(offsetof(moo_Vec3, x) == 16, "moo_Vec3::x offset changed");
static_assert(offsetof(moo_Vec3, y) == 24, "moo_Vec3::y offset changed");
static_assert(offsetof(moo_Vec3, z) == 32, "moo_Vec3::z offset changed");
static_assert(LUA_TVEC3 >= LUA_TSTRING, "LUA_TVEC3 must sit in the collectable GC tag range (>= LUA_TSTRING)");

#define sizevec3() (sizeof(moo_Vec3))

#define ttisvec3(o) (ttype(o) == LUA_TVEC3)
#define gco2vec3(o) check_exp((o)->gch.tt == LUA_TVEC3, cast_to(moo_Vec3*, (o)))
#define vec3value(o) check_exp(ttisvec3(o), gco2vec3((o)->value.gc))

#define setvec3value(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        moo_Vec3* i_v = (x); \
        i_o->value.gc = cast_to(GCObject*, i_v); \
        i_o->tt = LUA_TVEC3; \
        checkliveness((L)->global, i_o); \
    }

// Value equality compares the three Q48.16 components, never pointer identity or gcseq.
inline bool moo_vec3equal(const moo_Vec3* a, const moo_Vec3* b)
{
    return a->x == b->x && a->y == b->y && a->z == b->z;
}

LUAI_FUNC moo_Vec3* moo_newvec3(lua_State* L, int64_t x, int64_t y, int64_t z);
LUAI_FUNC void moo_freevec3(lua_State* L, moo_Vec3* v, struct lua_Page* page);
