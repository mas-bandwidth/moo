// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: the vec3 library. Each function is fixed's function on the raw Q48.16 components, so the bits are fixed3d's.
#include "lualib.h"

#include "lobject.h"
#include "moo_vec3.h"

#include "fixed/fixed_vec.h"

fixVec3 moo_vec3_check(lua_State* L, int idx)
{
    lua_Number raw[3];
    if (!moo_tovec3(L, idx, raw))
        luaL_typeerrorL(L, idx, "vec3");
    return FIX_LITERAL(fixVec3){raw[0], raw[1], raw[2]};
}

void moo_vec3_push(lua_State* L, fixVec3 v)
{
    moo_pushvec3(L, v.x, v.y, v.z);
}

namespace
{

int vec3_new(lua_State* L)
{
    fixed_t x = luaL_optnumber(L, 1, 0);
    fixed_t y = luaL_optnumber(L, 2, 0);
    fixed_t z = luaL_optnumber(L, 3, 0);
    moo_pushvec3(L, x, y, z);
    return 1;
}

// exact 128-bit accumulation, one rounding
int vec3_dot(lua_State* L)
{
    fixVec3 a = moo_vec3_check(L, 1);
    fixVec3 b = moo_vec3_check(L, 2);
    lua_pushnumber(L, fixDot(a, b));
    return 1;
}

int vec3_cross(lua_State* L)
{
    fixVec3 a = moo_vec3_check(L, 1);
    fixVec3 b = moo_vec3_check(L, 2);
    moo_vec3_push(L, fixCross(a, b));
    return 1;
}

int vec3_length(lua_State* L)
{
    lua_pushnumber(L, fixLength(moo_vec3_check(L, 1)));
    return 1;
}

int vec3_normalize(lua_State* L)
{
    moo_vec3_push(L, fixNormalize(moo_vec3_check(L, 1)));
    return 1;
}

const luaL_Reg veclib[] = {
    {"new", vec3_new},
    {"create", vec3_new}, // moo: Luau's vector.create
    {"dot", vec3_dot},
    {"cross", vec3_cross},
    {"length", vec3_length},
    {"magnitude", vec3_length}, // moo: Luau's vector.magnitude
    {"normalize", vec3_normalize},
    {NULL, NULL},
};

} // namespace

int luaopen_vec3(lua_State* L)
{
    luaL_register(L, "vec3", veclib);
    return 1;
}
