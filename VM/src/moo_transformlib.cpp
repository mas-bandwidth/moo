// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: the transform operations and the `transform` library of layer 3b (docs/DESIGN.md "vec3, quat and transform
// (layer 3b)", "Operations"; docs/layer3b/moo-v2-l3b-transform.md). Every function is fixed's function of the same
// name, with the one position add or subtract it makes done by moo's saturating add (moo_transform.h says why), so
// the bits match fixed3d's b3Transform functions wherever fixed's are defined. Opened by luaL_openlibs (linit.cpp).
#include "lualib.h"

#include "moo_number.h"
#include "moo_transform.h"

#include "fixed/fixed_vec.h"

// moo_numadd and moo_numsub component by component: the same rule as vec3 + and - (lvmutils.cpp, mooVec3Add)
static fixVec3 mooTransformAdd(fixVec3 a, fixVec3 b)
{
    return FIX_LITERAL(fixVec3){moo_numadd(a.x, b.x), moo_numadd(a.y, b.y), moo_numadd(a.z, b.z)};
}

static fixVec3 mooTransformSub(fixVec3 a, fixVec3 b)
{
    return FIX_LITERAL(fixVec3){moo_numsub(a.x, b.x), moo_numsub(a.y, b.y), moo_numsub(a.z, b.z)};
}

// fixMulTransforms: out.p = fixVecAdd(fixRotateVector(a.q, b.p), a.p), out.q = fixMulQuat(a.q, b.q)
fixTransform moo_transform_mul(fixTransform a, fixTransform b)
{
    fixTransform out;
    out.p = mooTransformAdd(fixRotateVector(a.q, b.p), a.p);
    out.q = fixMulQuat(a.q, b.q);
    return out;
}

// fixInvertTransform: out.p = fixInvRotateVector(t.q, fixVecNeg(t.p)), out.q = fixConjugate(t.q). Negation is exact
// on the domain [-INT64_MAX, INT64_MAX], so there is no add to saturate and this is fixed's function verbatim.
fixTransform moo_transform_inverse(fixTransform t)
{
    return fixInvertTransform(t);
}

// fixTransformPoint: fixVecAdd(fixRotateVector(t.q, v), t.p)
fixVec3 moo_transform_point(fixTransform t, fixVec3 v)
{
    return mooTransformAdd(fixRotateVector(t.q, v), t.p);
}

// fixInvTransformPoint: fixInvRotateVector(t.q, fixVecSub(v, t.p))
fixVec3 moo_transform_invpoint(fixTransform t, fixVec3 v)
{
    return fixInvRotateVector(t.q, mooTransformSub(v, t.p));
}

namespace
{

fixTransform mooCheckTransform(lua_State* L, int idx)
{
    lua_Number raw[7];
    if (!moo_totransform(L, idx, raw))
        luaL_typeerrorL(L, idx, "transform");
    return FIX_LITERAL(fixTransform){{raw[0], raw[1], raw[2]}, {{raw[3], raw[4], raw[5]}, raw[6]}};
}

fixVec3 mooCheckVec3(lua_State* L, int idx)
{
    lua_Number raw[3];
    if (!moo_tovec3(L, idx, raw))
        luaL_typeerrorL(L, idx, "vec3");
    return FIX_LITERAL(fixVec3){raw[0], raw[1], raw[2]};
}

void mooPushTransform(lua_State* L, fixTransform t)
{
    moo_pushtransform(L, t.p.x, t.p.y, t.p.z, t.q.v.x, t.q.v.y, t.q.v.z, t.q.s);
}

void mooPushVec3(lua_State* L, fixVec3 v)
{
    moo_pushvec3(L, v.x, v.y, v.z);
}

// transform.new(p, q): the position vec3 p and the rotation quat q. Omitted parts are the identity's (the zero
// vector and quat (0, 0, 0, 1)), so transform.new() is the identity. The rotation is taken as given, as quat.new's
// components are: no normalization.
int transform_new(lua_State* L)
{
    fixTransform t = fixTransform_identity;
    if (!lua_isnoneornil(L, 1))
        t.p = mooCheckVec3(L, 1);
    if (!lua_isnoneornil(L, 2))
    {
        lua_Number raw[4];
        if (!moo_toquat(L, 2, raw))
            luaL_typeerrorL(L, 2, "quat");
        t.q = FIX_LITERAL(fixQuat){{raw[0], raw[1], raw[2]}, raw[3]};
    }
    mooPushTransform(L, t);
    return 1;
}

// transform.inverse(t) is fixInvertTransform.
int transform_inverse(lua_State* L)
{
    mooPushTransform(L, moo_transform_inverse(mooCheckTransform(L, 1)));
    return 1;
}

// transform.point(t, v) is fixTransformPoint, the same as t * v.
int transform_point(lua_State* L)
{
    fixTransform t = mooCheckTransform(L, 1);
    mooPushVec3(L, moo_transform_point(t, mooCheckVec3(L, 2)));
    return 1;
}

// transform.direction(t, v) is fixRotateVector(t.q, v): a direction turns with the rotation and ignores the position.
int transform_direction(lua_State* L)
{
    fixTransform t = mooCheckTransform(L, 1);
    mooPushVec3(L, fixRotateVector(t.q, mooCheckVec3(L, 2)));
    return 1;
}

// transform.invpoint(t, v) is fixInvTransformPoint.
int transform_invpoint(lua_State* L)
{
    fixTransform t = mooCheckTransform(L, 1);
    mooPushVec3(L, moo_transform_invpoint(t, mooCheckVec3(L, 2)));
    return 1;
}

// transform.invdirection(t, v) is fixInvRotateVector(t.q, v).
int transform_invdirection(lua_State* L)
{
    fixTransform t = mooCheckTransform(L, 1);
    mooPushVec3(L, fixInvRotateVector(t.q, mooCheckVec3(L, 2)));
    return 1;
}

const luaL_Reg transformlib[] = {
    {"new", transform_new},
    {"inverse", transform_inverse},
    {"point", transform_point},
    {"direction", transform_direction},
    {"invpoint", transform_invpoint},
    {"invdirection", transform_invdirection},
    {NULL, NULL},
};

} // namespace

int luaopen_transform(lua_State* L)
{
    luaL_register(L, "transform", transformlib);
    return 1;
}
