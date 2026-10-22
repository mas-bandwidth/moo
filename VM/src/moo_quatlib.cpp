// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: the `quat` library of layer 3b (docs/DESIGN.md "vec3, quat and transform (layer 3b)", "Operations";
// docs/layer3b/moo-v2-l3b-quat.md). Every function calls fixed's C function directly on the four raw Q48.16
// components (fixConjugate, fixNLerp), so the bits match fixed3d by construction. quat.slerp is refused by name
// until fixed has a slerp (DESIGN.md: it is added upstream first). Opened by luaL_openlibs (linit.cpp).
#include "lualib.h"

#include "moo_number.h"

#include "fixed/fixed_vec.h"

namespace
{

// a quat argument through the C API's raw components; anything else is the named "quat" type error
fixQuat mooCheckQuat(lua_State* L, int idx)
{
    lua_Number raw[4];
    if (!moo_toquat(L, idx, raw))
        luaL_typeerrorL(L, idx, "quat");
    return FIX_LITERAL(fixQuat){{raw[0], raw[1], raw[2]}, raw[3]};
}

void mooPushQuat(lua_State* L, fixQuat q)
{
    moo_pushquat(L, q.v.x, q.v.y, q.v.z, q.s);
}

// quat.new(x, y, z, w): the vector part x, y, z and the scalar w (fixed's s). Omitted components are the
// identity's, (0, 0, 0, 1), so quat.new() is the identity.
int quat_new(lua_State* L)
{
    fixed_t x = luaL_optnumber(L, 1, 0);
    fixed_t y = luaL_optnumber(L, 2, 0);
    fixed_t z = luaL_optnumber(L, 3, 0);
    fixed_t w = luaL_optnumber(L, 4, MOO_NUMBER_ONE);
    moo_pushquat(L, x, y, z, w);
    return 1;
}

// quat.conjugate(q) is fixed's fixConjugate.
int quat_conjugate(lua_State* L)
{
    mooPushQuat(L, fixConjugate(mooCheckQuat(L, 1)));
    return 1;
}

// quat.nlerp(a, b, alpha) is fixed's fixNLerp. alpha must be in [0, 1]: fixed asserts it (fixVecLerp), so moo
// refuses anything else by name rather than reach an assert or an undefined result.
int quat_nlerp(lua_State* L)
{
    fixQuat a = mooCheckQuat(L, 1);
    fixQuat b = mooCheckQuat(L, 2);
    fixed_t alpha = luaL_checknumber(L, 3);
    luaL_argcheck(L, alpha >= 0 && alpha <= MOO_NUMBER_ONE, 3, "alpha must be in [0, 1]");
    mooPushQuat(L, fixNLerp(a, b, alpha));
    return 1;
}

// quat.slerp does not exist in fixed at moo's pin (docs/DESIGN.md "Operations"); refused by name until it does.
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
