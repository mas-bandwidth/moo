// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: layer 3c, the narrow types' operators, component reads and library helpers (moo_narrowlib.cpp; docs/DESIGN.md
// "Layer 3c as built").
#pragma once

#include "lobject.h"

#include "fixed/fixed.h"
#include "fixed/fixed_vec.h"

// the value at idx as fixed's type, or the argument type error
LUAI_FUNC fixVec2 moo_vec2_check(lua_State* L, int idx);
LUAI_FUNC fixMatrix3 moo_mat3_check(lua_State* L, int idx);
LUAI_FUNC fixPlane moo_plane_check(lua_State* L, int idx);
LUAI_FUNC fixAABB moo_aabb_check(lua_State* L, int idx);
LUAI_FUNC fixed30_t moo_fix30_check(lua_State* L, int idx);

LUAI_FUNC void moo_mat3_push(lua_State* L, fixMatrix3 m);
LUAI_FUNC void moo_aabb_push(lua_State* L, fixAABB a);

// op (a TMS) on rb and rc when either is a mat3 and the pair is one of mat3's operators: the result is in ra and the
// return is true; false leaves everything to the ordinary arithmetic (luaV_doarithimpl)
LUAI_FUNC bool moo_narrow_arith(lua_State* L, int op, StkId ra, const TValue* rb, const TValue* rc);

// t[key] when t is a vec2, mat3, plane or aabb and key one of its component names: the value is in val and the
// return is true
LUAI_FUNC bool moo_narrow_index(lua_State* L, const TValue* t, const TValue* key, StkId val);
