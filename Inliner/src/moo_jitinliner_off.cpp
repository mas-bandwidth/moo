// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: the JIT bytecode inliner is excluded from the build (MOO_JIT_INLINER, docs/DESIGN.md "Native codegen"). It folds
// number constants at run time as doubles, and moo's number is Q48.16: code that computes wrong answers is worse than
// none. This file is the whole of Luau.Inliner in moo; enabling it leaves the VM on its interpreter path.
#include "luajitinliner.h"
#include "Luau/JitInliner.h"

// flags the tests set by name; they change nothing while the inliner is excluded
LUAU_FASTFLAGVARIABLE(LuauBytecodeFold)

void luau_enable_jit_inliner(lua_State* L)
{
    (void)L;
}

void luau_disable_jit_inliner(lua_State* L)
{
    (void)L;
}

namespace Luau
{
namespace JitInliner
{

void setup(lua_State* L)
{
    (void)L;
}

void disable(lua_State* L)
{
    (void)L;
}

} // namespace JitInliner
} // namespace Luau
