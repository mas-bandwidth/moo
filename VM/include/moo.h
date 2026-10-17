// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
#pragma once

#include "lua.h"

// moo's additions to the C API. Each one exists because a Luau behaviour that depended on
// the machine (an address, a clock) became a host input; see docs/DETERMINISM.md.

// The seed math.random starts from when no seed is given (docs/DETERMINISM.md, 10).
#define MOO_DEFAULT_RANDOM_SEED 0

// Sets the math.random seed for the whole VM (every thread of L's global state) and reseeds
// the generator now. luaL_openlibs and luaopen_math seed from the last value set here, so
// the call may come before or after the libraries are opened. math.randomseed in a script
// still reseeds the generator.
LUA_API void moo_setrandomseed(lua_State* L, uint64_t seed);
