// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// GC: a vec3, quat or transform result survives a stack shrink inside a GC step.

#include "lua.h"
#include "lualib.h"
#include "lgc.h"
#include "lstate.h"
#include "moo_number.h"
#include "Luau/Compiler.h"
#include "MooHost.h"
#include "doctest.h"

#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

TEST_SUITE_BEGIN("gc");

// moo: a vec3 or quat operator stores its new object before the GC check (VM/src/lvmutils.cpp, mooVec3SetResult).
// A GC step in the propagate phase can shrink the running thread's stack (lgc.cpp, shrinkstack), so a register
// pointer taken before luaC_checkGC is stale after it; storing through it wrote into the freed stack and the
// register kept its old value. These cases grow the stack, return to a shallow frame, keep the collector due on
// every allocation, and check every result of every operation that ran while the stack moved.

namespace
{

// An allocator that moves every reallocation and keeps each freed block, filled with a poison byte, until the state
// is closed: a stale stack pointer then writes into a live quarantined block (no use after free in the test) and the
// new stack never sees the write, whatever the platform's realloc would have done in place.
struct Quarantine
{
    std::vector<void*> blocks;

    ~Quarantine()
    {
        for (void* b : blocks)
            free(b);
    }
};

void* quarantineAlloc(void* ud, void* ptr, size_t osize, size_t nsize)
{
    Quarantine* q = static_cast<Quarantine*>(ud);
    void* fresh = nullptr;
    if (nsize != 0)
    {
        fresh = malloc(nsize);
        if (!fresh)
            return nullptr;
        if (ptr)
            memcpy(fresh, ptr, osize < nsize ? osize : nsize);
    }
    if (ptr)
    {
        memset(ptr, 0xab, osize);
        q->blocks.push_back(ptr);
    }
    return fresh;
}

// Runs op(i) for i = 1..n with the collector due before every call; returns how many calls saw the stack
// shrunk (so every live stack pointer moved, under this allocator) and requires every call's result check (done in the script) to pass.
int runWhileStackShrinks(const char* source, int n)
{
    Quarantine quarantine;
    lua_State* L = lua_newstate(quarantineAlloc, &quarantine);
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    std::string bytecode = Luau::compile(source);
    REQUIRE(luau_load(L, "=moo-gc-stack", bytecode.data(), bytecode.size(), 0) == 0);
    REQUIRE(lua_pcall(L, 0, 0, 0) == 0);

    int moved = 0;
    for (int i = 1; i <= n; ++i)
    {
        // grow the stack well past three times what op uses, so the next propagate phase halves it
        lua_getglobal(L, "deep");
        lua_pushnumber(L, fixed_t(2000) * MOO_NUMBER_ONE);
        REQUIRE(lua_pcall(L, 1, 0, 0) == 0);

        int stackBefore = L->stacksize;
        L->global->GCthreshold = 0; // every allocation in op is due for a GC step
        lua_getglobal(L, "op");
        lua_pushnumber(L, fixed_t(i) * MOO_NUMBER_ONE);
        int status = lua_pcall(L, 1, 1, 0);
        CAPTURE(i);
        REQUIRE(status == 0);
        CHECK(lua_toboolean(L, -1));
        lua_pop(L, 1);
        if (L->stacksize != stackBefore)
            ++moved;
    }
    lua_close(L);
    return moved;
}

const char* kDeep = "function deep(n) if n == 0 then return 0 end return 1 + deep(n - 1) end ";

} // namespace

TEST_CASE("Vec3ResultSurvivesStackShrinkInGcStep")
{
    std::string source = std::string(kDeep) +
                         "function op(i) "
                         "  local a, b = vec3.new(i, 0, 0), vec3.new(0, i, 0) "
                         "  local c = a + b "
                         "  local d = c * 2 "
                         "  return c == vec3.new(i, i, 0) and d == vec3.new(2 * i, 2 * i, 0) "
                         "end";
    // the witness: the stack really moved during some operation, so the cases above ran on a stale-pointer path
    CHECK(runWhileStackShrinks(source.c_str(), 200) > 0);
}

TEST_CASE("QuatResultSurvivesStackShrinkInGcStep")
{
    std::string source = std::string(kDeep) +
                         "function op(i) "
                         "  local h = quat.new(0, 0, 1, 0) "
                         "  local c = h * h "
                         "  local v = h * vec3.new(i, 0, 0) "
                         "  return c == quat.new(0, 0, 0, -1) and v == vec3.new(-i, 0, 0) "
                         "end";
    CHECK(runWhileStackShrinks(source.c_str(), 200) > 0);
}

TEST_CASE("TransformResultSurvivesStackShrinkInGcStep")
{
    std::string source = std::string(kDeep) +
                         "function op(i) "
                         "  local a = transform.new(vec3.new(i, 0, 0)) "
                         "  local c = a * a "
                         "  local p = a * vec3.new(0, i, 0) "
                         "  return c == transform.new(vec3.new(2 * i, 0, 0)) and p == vec3.new(i, i, 0) "
                         "end";
    CHECK(runWhileStackShrinks(source.c_str(), 200) > 0);
}

TEST_SUITE_END();
