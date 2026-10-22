// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: the transform object of layer 3b (docs/DESIGN.md "vec3, quat and transform (layer 3b)", option (a) of
// "Representations costed"). It copies the vec3 and quat objects' decisions (docs/layer3b/moo-v2-l3b-vec3-object.md,
// docs/layer3b/moo-v2-l3b-quat-object.md) and adds its operations (docs/layer3b/moo-v2-l3b-transform.md).
//
// A transform is one immutable GC object: the common header (with its creation number, docs/DETERMINISM.md 1) and
// fixed's fixTransform, a position fixVec3 and a rotation fixQuat, 56 bytes of payload. It holds no reference to
// another object, so the GC treats it as a leaf, as it treats a string. Its tag is LUA_TTRANSFORM (lua.h).
#pragma once

#include "lobject.h"

#include "fixed/fixed.h"
#include "fixed/fixed_vec.h"

#include <stddef.h>

typedef struct MooTransform
{
    CommonHeader;
    // 5 bytes of padding: the header is gcseq (8) then tt, marked, memcat (3), and the payload is 8-aligned

    fixVec3 p;
    fixQuat q;
} MooTransform;

// The payload is fixed's fixTransform (which fixed3d's b3Transform is), component for component (DESIGN.md table:
// 56 bytes). An operation copies the components into a fixTransform by value and calls fixed's functions on it; it
// never casts the object's address.
static_assert(sizeof(fixTransform) == 56, "fixTransform is a fixVec3 and a fixQuat");
static_assert(offsetof(MooTransform, p) == sizeof(GCheader), "the payload starts right after the GC header");
static_assert(offsetof(MooTransform, q) == offsetof(MooTransform, p) + sizeof(fixVec3), "the rotation follows the position");
static_assert(offsetof(MooTransform, q.s) == offsetof(MooTransform, q.v) + sizeof(fixVec3), "the scalar follows the vector");
static_assert(sizeof(MooTransform) - offsetof(MooTransform, p) == 56, "transform payload is 56 bytes (docs/DESIGN.md, vec3, quat and transform)");
// totalbytes counts this size per transform (docs/DETERMINISM.md 5), so it is pinned like every struct in moo_layout.cpp
static_assert(sizeof(MooTransform) == 72, "transform object layout changed");

// Longest tostring: "transform(vec3(" + 3 numbers + "), quat(" + 4 numbers + "))", each number at most 34
// characters (moo_numtostr), with ", " between numbers.
#define MOO_TRANSFORM_TOSTR_MAX (15 + 3 * 34 + 2 * 2 + 8 + 4 * 34 + 3 * 2 + 2)

// Allocates a transform (luaM_newgcofixed, then luaC_init, which takes the creation number) and stores the seven
// components; the object is never written again. In lobject.cpp.
LUAI_FUNC MooTransform* moo_transform_new(lua_State* L, fixTransform t);

// The payload as fixed's fixTransform, copied component for component.
LUAI_FUNC fixTransform moo_transform_payload(const MooTransform* t);

// 1 when the seven raw components are equal, else 0: equality is by value, never by address or creation number
// (vec3 note, decision 3). In lobject.cpp.
LUAI_FUNC int moo_transform_equal(const MooTransform* a, const MooTransform* b);

// Writes "transform(vec3(px, py, pz), quat(qx, qy, qz, qs))", each component through moo_numtostr (the shortest
// decimal that reads back exactly, docs/DESIGN.md "Number formatting"), into buf of at least
// MOO_TRANSFORM_TOSTR_MAX bytes; returns the end, not terminated, as moo_numtostr does. In lobject.cpp.
LUAI_FUNC char* moo_transform_tostring(char* buf, const MooTransform* t);

// The 32-bit table hash of the seven raw components, before hashpow2, so equal transforms are one key (vec3 note,
// decision 4). In ltable.cpp.
LUAI_FUNC unsigned int moo_transform_hash(const MooTransform* t);

// The operations (docs/DESIGN.md "Operations"; docs/layer3b/moo-v2-l3b-transform.md, decision 2). Each is fixed's
// function of the same name with its final position add or subtract done by moo's saturating add (moo_numadd,
// moo_numsub), as vec3 + and - are: fixed's fixVecAdd/fixVecSub are plain int64 arithmetic at this pin ("Saturation,
// stated precisely"). Wherever fixed's add does not overflow, the bits are fixed's; where it would overflow (undefined
// in C), moo saturates. The rotations are fixed's fixRotateVector, fixInvRotateVector, fixMulQuat and fixConjugate
// verbatim. In moo_transformlib.cpp.
LUAI_FUNC fixTransform moo_transform_mul(fixTransform a, fixTransform b);       // fixMulTransforms
LUAI_FUNC fixTransform moo_transform_inverse(fixTransform t);                   // fixInvertTransform
LUAI_FUNC fixVec3 moo_transform_point(fixTransform t, fixVec3 v);               // fixTransformPoint
LUAI_FUNC fixVec3 moo_transform_invpoint(fixTransform t, fixVec3 v);            // fixInvTransformPoint
