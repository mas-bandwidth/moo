// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
#pragma once

#include "lua.h"

// moo's additions to the C API: host inputs that replace Luau's machine-dependent behaviour (docs/DETERMINISM.md),
// and the boundary between host floating point and moo's number.

// moo's release version (CHANGELOG.md). Luau's _VERSION global stays "Luau"; this is the version of moo itself,
// which `moo --version` prints with the Luau release it is forked from.
#define MOO_VERSION_MAJOR 1
#define MOO_VERSION_MINOR 0
#define MOO_VERSION_PATCH 2
#define MOO_VERSION "1.0.2"
#define MOO_LUAU_VERSION "0.741"

// The seed math.random starts from when no seed is given (docs/DETERMINISM.md, 10).
#define MOO_DEFAULT_RANDOM_SEED 0

// Sets the math.random seed for the whole VM (every thread of L's global state) and reseeds
// the generator now. luaL_openlibs and luaopen_math seed from the last value set here, so
// the call may come before or after the libraries are opened. math.randomseed in a script
// still reseeds the generator.
LUA_API void moo_setrandomseed(lua_State* L, uint64_t seed);

// The creation number of the collectable value at idx (a table, function, userdata, thread,
// buffer, string, ...): the per-VM sequence number it was given when it was allocated. The main
// thread is 0. Non-collectable values (nil, boolean, number, light userdata) give 0.
// tostring prints it as "table: #123".
LUA_API uint64_t moo_objectseq(lua_State* L, int idx);

// The boundary between host floating point and moo's number (docs/DESIGN.md, "Boundary"). A number
// is the raw signed 64-bit Q48.16 value (fixed's fixed_t); its domain is [-INT64_MAX, INT64_MAX].
#define MOO_NUMBER_OK 0
#define MOO_NUMBER_NAN 1          // the input is a NaN
#define MOO_NUMBER_INFINITE 2     // the input is an infinity
#define MOO_NUMBER_OUT_OF_RANGE 3 // the input rounds to a value outside the domain

// Decodes the IEEE-754 bits of x in integer arithmetic and rounds once to the nearest Q48.16 value,
// ties to even. Negative zero gives 0; subnormals and anything below half a unit give 0. Writes *out
// only when it returns MOO_NUMBER_OK.
LUA_API int moo_fromdouble(double x, int64_t* out);
LUA_API int moo_fromfloat(float x, int64_t* out);

// The double nearest to the number, ties to even: exact for magnitudes below 2^37.
LUA_API double moo_todouble(int64_t n);

// The float nearest to the number, ties to even, as the C float whose bits are the nearest
// binary32 bit pattern (docs/DESIGN.md, "Boundary"). Every number is finite in float, so there is
// no overflow case. buffer.writef32 and string.pack("f") encode by the same rule.
LUA_API float moo_tofloat(int64_t n);
