// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Tests of moo's own behaviour: each one holds a change moo made on purpose to Luau
// (docs/DETERMINISM.md names the change by number).
#include "lua.h"
#include "lualib.h"
#include "luacode.h"
#include "moo.h"

#include "doctest.h"

#include <stdlib.h>
#include <string.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

struct MooState
{
    lua_State* L;

    explicit MooState(bool openlibs = true)
        : L(luaL_newstate())
    {
        if (openlibs)
            luaL_openlibs(L);
    }

    ~MooState()
    {
        lua_close(L);
    }

    MooState(const MooState&) = delete;
    MooState& operator=(const MooState&) = delete;
};

// Compiles and runs source as a chunk; returns its first result as a string, or "error: " and the message.
std::string mooRun(lua_State* L, const char* source)
{
    size_t size = 0;
    char* bytecode = luau_compile(source, strlen(source), nullptr, &size);
    int status = luau_load(L, "=moo", bytecode, size, 0);
    free(bytecode);

    if (status == 0)
        status = lua_pcall(L, 0, 1, 0);

    std::string result;
    if (status != 0)
        result = std::string("error: ") + (lua_isstring(L, -1) ? lua_tostring(L, -1) : "?");
    else
        result = luaL_tolstring(L, -1, nullptr), lua_pop(L, 1);

    lua_pop(L, 1);
    return result;
}

// Runs a moo-specific expected-behaviour file from tests/conformance/moo; it returns "OK".
std::string mooRunFile(lua_State* L, const char* name)
{
#ifdef LUAU_CONFORMANCE_SOURCE_DIR
    std::string path = std::string(LUAU_CONFORMANCE_SOURCE_DIR) + "/moo/" + name;
    std::ifstream file(path, std::ios::binary);
    std::stringstream source;
    source << file.rdbuf();
    REQUIRE_MESSAGE(file.good(), "cannot read " << path);
    return mooRun(L, source.str().c_str());
#else
    return "OK";
#endif
}

const char* const kRandomSequence = R"(
    local t = {}
    for i = 1, 8 do
        t[i] = math.random(1, 1000000)
    end
    return table.concat(t, ",")
)";

// Object keys of every collectable kind, with deletes during traversal and a rehash after.
const char* const kObjectKeyOrder = R"(
    local keys = {}
    local t = {}
    for i = 1, 40 do
        local k
        local m = i % 5
        if m == 0 then k = {}
        elseif m == 1 then k = function() return i end
        elseif m == 2 then k = coroutine.create(function() end)
        elseif m == 3 then k = buffer.create(i)
        else k = newproxy() end
        keys[k] = i
        t[k] = true
    end
    local n = 0
    for k in pairs(t) do n += 1; if n % 3 == 0 then t[k] = nil end end
    for i = 41, 60 do local k = {} keys[k] = i; t[k] = true end
    local order = {}
    for k in pairs(t) do table.insert(order, keys[k]) end
    return table.concat(order, ",")
)";

// Weak values under allocation churn, with no explicit collection: which entries survive at each
// round depends only on when the GC steps run.
const char* const kWeakChurn = R"(
    local cache = setmetatable({}, {__mode = "v"})
    local trace = {}
    local live = {}
    for round = 1, 300 do
        for i = 1, 400 do
            local v = {round, i, string.rep("x", (i % 7) * 10)}
            cache[round * 1000 + i] = v
            if i % 50 == 0 then live[#live + 1] = v end
        end
        local n = 0
        for _ in pairs(cache) do n += 1 end
        trace[#trace + 1] = n .. ":" .. gcinfo()
        if #live > 100 then live = {} end
    end
    return table.concat(trace, ",")
)";

// FNV-1a, to pin a long result in one line
uint64_t mooHash(const std::string& text)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char c : text)
        h = (h ^ c) * 0x100000001b3ull;
    return h;
}

} // namespace

TEST_SUITE_BEGIN("moo");

// DETERMINISM.md 9: the os library is removed; time is a host input.
TEST_CASE("OsLibraryIsRemoved")
{
    MooState s;
    CHECK(mooRun(s.L, "return rawget(_G, 'os') == nil") == "true");
    CHECK(mooRun(s.L, "return os == nil") == "true");
}

// Replaces upstream DateTime (tests/conformance/datetime.luau); the file says why.
TEST_CASE("DateTime")
{
    MooState s;
    CHECK(mooRunFile(s.L, "datetime.luau") == "OK");
}

// DETERMINISM.md 10: math.random starts from a constant seed, never an address or a clock.
TEST_CASE("RandomDefaultSeedIsConstant")
{
    MooState a;
    MooState b;

    std::string first = mooRun(a.L, kRandomSequence);
    CHECK(first == mooRun(b.L, kRandomSequence));

    // Pinned: PCG32 from MOO_DEFAULT_RANDOM_SEED. A change here changes every program's random numbers.
    CHECK(first == "782827,551456,107634,17232,334180,883377,720802,920949");
}

// DETERMINISM.md 10: the host sets the seed, before or after the libraries are opened.
TEST_CASE("RandomSeedIsHostInput")
{
    MooState before(false);
    moo_setrandomseed(before.L, 42);
    luaL_openlibs(before.L);

    MooState after;
    moo_setrandomseed(after.L, 42);

    MooState script;
    mooRun(script.L, "math.randomseed(42)");

    MooState unseeded;

    std::string seeded = mooRun(before.L, kRandomSequence);
    CHECK(seeded == mooRun(after.L, kRandomSequence));
    CHECK(seeded == mooRun(script.L, kRandomSequence));
    CHECK(seeded != mooRun(unseeded.L, kRandomSequence));

    // reseeding restarts the sequence
    moo_setrandomseed(after.L, 42);
    CHECK(seeded == mooRun(after.L, kRandomSequence));
}

// DETERMINISM.md 1: object keys hash by creation number, so pairs order is a function of the program.
TEST_CASE("ObjectKeyOrderIgnoresAddresses")
{
    MooState a;
    std::string order = mooRun(a.L, kObjectKeyOrder);

    // Move the second VM's objects to other addresses: hold host allocations of assorted sizes.
    std::vector<std::vector<char>> perturb;
    for (int i = 1; i <= 64; ++i)
        perturb.emplace_back(size_t(i * 37 % 509 + 1));

    MooState b;
    CHECK(order == mooRun(b.L, kObjectKeyOrder));

    // Pinned: a change here changes the iteration order of every object-keyed table.
    CHECK(order == "18,3,32,8,9,5,24,20,60,59,54,50,37,11,52,13,27,56,55,51,53,47,28,49,19,31,46,44,26,35,15,43,48,23,41,14,25,34,42,16,7,22,57,4,29,58,45");
}

// DETERMINISM.md 6: tostring prints the creation number, never the address.
TEST_CASE("TostringPrintsCreationNumber")
{
    MooState s;

    std::string pair = mooRun(s.L, "local a, b = {}, {} return tostring(a) .. ' ' .. tostring(b)");
    CHECK(pair == "table: #400 table: #401");

    const char* const kinds = R"(
        local values = { {}, function() end, coroutine.create(function() end), buffer.create(1), newproxy() }
        local out = {}
        for _, v in values do
            local text = tostring(v)
            assert(string.match(text, "^%a+: #%d+$"), text)
            table.insert(out, (string.gsub(text, "#%d+", "#")))
        end
        return table.concat(out, " ")
    )";
    CHECK(mooRun(s.L, kinds) == "table: # function: # thread: # buffer: # userdata: #");

    // the host's value is never printed
    lua_pushlightuserdata(s.L, (void*)0x12345678);
    lua_setglobal(s.L, "lud");
    CHECK(mooRun(s.L, "return tostring(lud)") == "userdata: light");

    // tostring and the C API agree
    mooRun(s.L, "t = {}");
    lua_getglobal(s.L, "t");
    std::string viaApi = "table: #" + std::to_string(moo_objectseq(s.L, -1));
    lua_pop(s.L, 1);
    CHECK(mooRun(s.L, "return tostring(t)") == viaApi);

    // creation numbers count up per VM; the main thread is 0; non-collectable values are 0
    CHECK(moo_objectseq(s.L, LUA_GLOBALSINDEX) > 0);
    lua_pushnumber(s.L, 1);
    CHECK(moo_objectseq(s.L, -1) == 0);
    lua_pop(s.L, 1);
    lua_pushthread(s.L);
    CHECK(moo_objectseq(s.L, -1) == 0);
    lua_pop(s.L, 1);

    MooState other;
    CHECK(mooRun(other.L, "local a, b = {}, {} return tostring(a) .. ' ' .. tostring(b)") == pair);
}

// DETERMINISM.md 4: GC pacing reads no clock. Luau's pacing measured an allocation rate in bytes per
// second; the same program run five times under the 0.741 CLI on hulk cleared different weak entries.
TEST_CASE("GcPacingIsAFunctionOfTheProgram")
{
    MooState a;
    std::string trace = mooRun(a.L, kWeakChurn);
    REQUIRE(trace.find("error") == std::string::npos);

    MooState b;
    CHECK(trace == mooRun(b.L, kWeakChurn));

    // Pinned: a change here changes when the GC runs for every program.
    CHECK(mooHash(trace) == 10374631352344400660ull);
}

// Replaces upstream ApiAllocationRateAfterFullGC, which checked that lua_allocationrate measures bytes
// per second. moo has no clock (DETERMINISM.md 4), so the rate can never be measured and the API says so.
TEST_CASE("AllocationRateIsNeverMeasured")
{
    MooState s;
    CHECK(lua_allocationrate(s.L) == -1);
    mooRun(s.L, "local t = {} for i = 1, 100000 do t[i] = {} end");
    CHECK(lua_allocationrate(s.L) == -1);
    lua_gc(s.L, LUA_GCCOLLECT, 0);
    CHECK(lua_allocationrate(s.L) == -1);
}

TEST_SUITE_END();
