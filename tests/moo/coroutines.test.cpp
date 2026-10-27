// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Coroutines: resume, yield, close, wrap and their refusals as programs see them (docs/DESIGN.md
// "State machines and TLA+").

#include "lua.h"
#include "lualib.h"
#include "doctest.h"
#include "MooHost.h"

TEST_SUITE_BEGIN("coroutines");

// Layer 5 coroutine transitions and refusals (DETERMINISM.md 21): each case pins one transition or one
// refused transition with its exact error text. The adversarial program tests/determinism/coroutines2.luau
// pins the same behaviours in the determinism suite; this file pins them at the C API boundary.

// DETERMINISM.md 21: resume -> running -> yield -> suspended, until return makes the coroutine dead.
TEST_CASE("CoroutineGenerator")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    const char* src = R"(
        local co = coroutine.create(function(a)
            local x = a
            for i = 1, 3 do
                x = coroutine.yield(x + i)
            end
            return x * 10
        end)
        local t = {}
        for i = 1, 4 do
            local ok, v = coroutine.resume(co, i * 3)
            t[#t + 1] = tostring(ok) .. ":" .. tostring(v) .. ":" .. coroutine.status(co)
        end
        return table.concat(t, " ")
    )";
    CHECK(mooRun(L, src) == "true:4:suspended true:8:suspended true:12:suspended true:120:dead");

    lua_close(L);
}

// DETERMINISM.md 21: a yield crosses a pcall boundary inside a coroutine.
TEST_CASE("CoroutineYieldAcrossPcall")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    const char* src = R"(
        local co = coroutine.create(function()
            local ok, v = pcall(function() return coroutine.yield("across") end)
            return tostring(ok) .. ":" .. tostring(v)
        end)
        local ok, v = coroutine.resume(co)
        return tostring(ok) .. ":" .. tostring(v)
    )";
    CHECK(mooRun(L, src) == "true:across");

    lua_close(L);
}

// DETERMINISM.md 21: an error thrown by a nested resume is caught by the parent.
TEST_CASE("CoroutineNestedResumeError")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    const char* src = R"(
        local co = coroutine.create(function()
            local B = coroutine.create(function() error("inner failure") end)
            local ok, e = coroutine.resume(B)
            return tostring(ok) .. ":" .. tostring(e):match("inner failure")
        end)
        local ok, v = coroutine.resume(co)
        return tostring(ok) .. ":" .. v
    )";
    CHECK(mooRun(L, src) == "true:false:inner failure");

    lua_close(L);
}

// DETERMINISM.md 21: a deep chain of coroutines, each resuming the next; the deepest yields and unwinds.
TEST_CASE("CoroutineDeepNesting")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    const char* src = R"(
        local deep = 0
        local function nest(k)
            if k == 0 then return coroutine.yield("bottom") end
            local inner = coroutine.create(nest)
            local ok, v = coroutine.resume(inner, k - 1)
            deep += 1
            return v
        end
        local top = coroutine.create(nest)
        local ok, v = coroutine.resume(top, 12)
        return tostring(ok) .. ":" .. tostring(v) .. ":" .. deep
    )";
    CHECK(mooRun(L, src) == "true:bottom:12");

    lua_close(L);
}

// DETERMINISM.md 21: close inside pcall, on a suspended and on an errored coroutine.
TEST_CASE("CoroutineCloseInsidePcall")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    const char* src = R"(
        local suspended = coroutine.create(function() coroutine.yield() end)
        coroutine.resume(suspended)
        local ok1, c1 = pcall(coroutine.close, suspended)
        local errored = coroutine.create(function() error("boom") end)
        coroutine.resume(errored)
        local ok2, c2, e2 = pcall(coroutine.close, errored)
        return tostring(ok1) .. ":" .. tostring(c1) .. ":" .. coroutine.status(suspended)
            .. " " .. tostring(ok2) .. ":" .. tostring(c2) .. ":" .. tostring(e2):match("boom")
            .. ":" .. coroutine.status(errored)
    )";
    CHECK(mooRun(L, src) == "true:true:dead true:false:boom:dead");

    lua_close(L);
}

// DETERMINISM.md 21: close of a dead coroutine is allowed and returns true.
TEST_CASE("CoroutineCloseDead")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    const char* src = R"(
        local dead = coroutine.create(function() end)
        coroutine.resume(dead)
        local ok, c = pcall(coroutine.close, dead)
        return tostring(ok) .. ":" .. tostring(c) .. ":" .. coroutine.status(dead)
    )";
    CHECK(mooRun(L, src) == "true:true:dead");

    lua_close(L);
}

// DETERMINISM.md 21: close of the running coroutine is refused, with its exact text.
TEST_CASE("CoroutineRefuseCloseRunning")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    const char* src = R"(
        local co
        co = coroutine.create(function()
            local ok, e = pcall(coroutine.close, co)
            return tostring(ok) .. ":" .. tostring(e)
        end)
        local ok, e = coroutine.resume(co)
        return tostring(ok) .. ":" .. e
    )";
    CHECK(mooRun(L, src) == "true:false:cannot close running coroutine");

    lua_close(L);
}

// DETERMINISM.md 21: resume of a dead coroutine is refused, with its exact text.
TEST_CASE("CoroutineRefuseResumeDead")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    const char* src = R"(
        local dead = coroutine.create(function() end)
        coroutine.resume(dead)
        local ok, r, e = pcall(coroutine.resume, dead)
        return tostring(ok) .. ":" .. tostring(r) .. ":" .. tostring(e)
    )";
    CHECK(mooRun(L, src) == "true:false:cannot resume dead coroutine");

    lua_close(L);
}

// DETERMINISM.md 21: resume of the running coroutine is refused, with its exact text.
TEST_CASE("CoroutineRefuseResumeRunning")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    const char* src = R"(
        local co
        co = coroutine.create(function()
            local ok, r, e = pcall(coroutine.resume, co)
            return tostring(ok) .. ":" .. tostring(r) .. ":" .. tostring(e)
        end)
        local ok, v = coroutine.resume(co)
        return v
    )";
    CHECK(mooRun(L, src) == "true:false:cannot resume running coroutine");

    lua_close(L);
}

// DETERMINISM.md 21: wrapping a dead coroutine throws, with its exact text.
TEST_CASE("CoroutineWrapDeadThrows")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    const char* src = R"(
        local w = coroutine.wrap(function()
            coroutine.yield("w1")
            return "w2"
        end)
        local first = w()
        local second = w()
        local ok, e = pcall(w)
        return tostring(first) .. " " .. tostring(second) .. " " .. tostring(ok) .. ":" .. tostring(e)
    )";
    CHECK(mooRun(L, src) == "w1 w2 false:cannot resume dead coroutine");

    lua_close(L);
}

// DETERMINISM.md 21: a yield inside a metamethod crosses a C-call boundary and is refused.
TEST_CASE("CoroutineRefuseYieldInMetamethod")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    const char* src = R"(
        local t = setmetatable({}, {__index = function() return coroutine.yield("meta") end})
        local co = coroutine.create(function()
            local ok, e = pcall(function() return t.x end)
            return tostring(ok) .. ":" .. tostring(e)
        end)
        local ok, e = coroutine.resume(co)
        return tostring(ok) .. ":" .. e
    )";
    CHECK(mooRun(L, src) == "true:false:attempt to yield across metamethod/C-call boundary");

    lua_close(L);
}

// DETERMINISM.md 21: a yield inside a table.sort comparator crosses a C-call boundary and is refused.
TEST_CASE("CoroutineRefuseYieldInSortComparator")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    const char* src = R"(
        local co = coroutine.create(function()
            local ok, e = pcall(function()
                table.sort({3, 1, 2}, function(a, b)
                    coroutine.yield(a)
                    return a < b
                end)
            end)
            return tostring(ok) .. ":" .. tostring(e)
        end)
        local ok, e = coroutine.resume(co)
        return tostring(ok) .. ":" .. e
    )";
    CHECK(mooRun(L, src) == "true:false:attempt to yield across metamethod/C-call boundary");

    lua_close(L);
}

TEST_SUITE_END();
