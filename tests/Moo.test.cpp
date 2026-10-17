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

TEST_SUITE_END();
