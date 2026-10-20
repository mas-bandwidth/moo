// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: the `vec3` library of layer 3b (docs/DESIGN.md "vec3, quat and transform (layer 3b)", "Operations").
// Every function calls fixed's C function directly on the three raw Q48.16 components: fixDot, fixCross,
// fixLength and fixNormalize, so the bits match fixed3d by construction. The library is opened by
// luaL_openlibs (linit.cpp); Luau's vector names create and magnitude are kept as aliases.
#include "lualib.h"

#include "lobject.h"
#include "moo_vec3.h"

#include "moo_number.h"

#include "fixed/fixed_vec.h"

namespace
{

// moo: a vec3 argument through the C API's raw components; anything else is the named "vec3" type error.
fixVec3 mooCheckVec3(lua_State* L, int idx)
{
    lua_Number raw[3];
    if (!moo_tovec3(L, idx, raw))
        luaL_typeerrorL(L, idx, "vec3");
    return FIX_LITERAL(fixVec3){raw[0], raw[1], raw[2]};
}

void mooPushVec3(lua_State* L, fixVec3 v)
{
    moo_pushvec3(L, v.x, v.y, v.z);
}

// vec3.new(x, y, z); Luau's vector.create is the same function.
int vec3_new(lua_State* L)
{
    fixed_t x = luaL_optnumber(L, 1, 0);
    fixed_t y = luaL_optnumber(L, 2, 0);
    fixed_t z = luaL_optnumber(L, 3, 0);
    moo_pushvec3(L, x, y, z);
    return 1;
}

// vec3.dot(a, b) is fixed's fixDot: exact 128-bit accumulation, one rounding.
int vec3_dot(lua_State* L)
{
    fixVec3 a = mooCheckVec3(L, 1);
    fixVec3 b = mooCheckVec3(L, 2);
    lua_pushnumber(L, fixDot(a, b));
    return 1;
}

// vec3.cross(a, b) is fixed's fixCross.
int vec3_cross(lua_State* L)
{
    fixVec3 a = mooCheckVec3(L, 1);
    fixVec3 b = mooCheckVec3(L, 2);
    mooPushVec3(L, fixCross(a, b));
    return 1;
}

// vec3.length(a) is fixed's fixLength; Luau's vector.magnitude is the same function.
int vec3_length(lua_State* L)
{
    fixVec3 a = mooCheckVec3(L, 1);
    lua_pushnumber(L, fixLength(a));
    return 1;
}

// vec3.normalize(a) is fixed's fixNormalize.
int vec3_normalize(lua_State* L)
{
    fixVec3 a = mooCheckVec3(L, 1);
    mooPushVec3(L, fixNormalize(a));
    return 1;
}

const luaL_Reg veclib[] = {
    {"new", vec3_new},
    {"create", vec3_new},       // moo: Luau vector.create alias
    {"dot", vec3_dot},
    {"cross", vec3_cross},
    {"length", vec3_length},
    {"magnitude", vec3_length}, // moo: Luau vector.magnitude alias
    {"normalize", vec3_normalize},
    {NULL, NULL},
};

} // namespace

int luaopen_vec3(lua_State* L)
{
    luaL_register(L, "vec3", veclib);
    return 1;
}
