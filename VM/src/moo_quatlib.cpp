// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: the quat library. Each function is fixed's function on the raw Q48.16 components, so the bits are fixed3d's.
#include "lualib.h"

#include "lobject.h"
#include "moo_number.h"
#include "moo_quat.h"

#include "fixed/fixed_vec.h"

fixQuat moo_quat_check(lua_State* L, int idx)
{
    lua_Number raw[4];
    if (!moo_toquat(L, idx, raw))
        luaL_typeerrorL(L, idx, "quat");
    return FIX_LITERAL(fixQuat){{raw[0], raw[1], raw[2]}, raw[3]};
}

void moo_quat_push(lua_State* L, fixQuat q)
{
    moo_pushquat(L, q.v.x, q.v.y, q.v.z, q.s);
}

namespace
{

// quat.new(x, y, z, w): w is fixed's scalar s. Omitted components are the identity's, so quat.new() is the identity.
int quat_new(lua_State* L)
{
    fixed_t x = luaL_optnumber(L, 1, 0);
    fixed_t y = luaL_optnumber(L, 2, 0);
    fixed_t z = luaL_optnumber(L, 3, 0);
    fixed_t w = luaL_optnumber(L, 4, MOO_NUMBER_ONE);
    moo_pushquat(L, x, y, z, w);
    return 1;
}

int quat_conjugate(lua_State* L)
{
    moo_quat_push(L, fixConjugate(moo_quat_check(L, 1)));
    return 1;
}

// fixed asserts alpha is in [0, 1] (fixVecLerp), so anything else is refused here by name
int quat_nlerp(lua_State* L)
{
    fixQuat a = moo_quat_check(L, 1);
    fixQuat b = moo_quat_check(L, 2);
    fixed_t alpha = luaL_checknumber(L, 3);
    luaL_argcheck(L, alpha >= 0 && alpha <= MOO_NUMBER_ONE, 3, "alpha must be in [0, 1]");
    moo_quat_push(L, fixNLerp(a, b, alpha));
    return 1;
}

// fixed has no slerp, so quat.slerp is a named error
int quat_slerp(lua_State* L)
{
    luaL_error(L, "quat.slerp waits for fixed's slerp");
    return 0;
}

const luaL_Reg quatlib[] = {
    {"new", quat_new},
    {"conjugate", quat_conjugate},
    {"nlerp", quat_nlerp},
    {"slerp", quat_slerp},
    {NULL, NULL},
};

} // namespace

int luaopen_quat(lua_State* L)
{
    luaL_register(L, "quat", quatlib);
    return 1;
}
