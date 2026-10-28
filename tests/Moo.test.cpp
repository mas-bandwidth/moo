// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Tests of moo's own behaviour: each one holds a change moo made on purpose to Luau
// (docs/DETERMINISM.md names the change by number).
#include "lua.h"
#include "lualib.h"
#include "luacode.h"
#include "moo.h"
#include "moo_number.h"

#include "fixed/fixed.h"

#include "doctest.h"
#include "MooHost.h" // moo: mooRun, mooRunFile and the moo_test helpers are shared with tests/moo

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <filesystem>
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

// The destructor log: the creation number of each userdata as its destructor runs, and 0 at each
// GC step boundary (0 is the main thread's number, never a userdata's).
std::vector<uint64_t> gDtorLog;
int gUserdataMade = 0;
const int kDtorTag = 7;

void mooLogDtor(lua_State* L, void* data)
{
    (void)L;
    uint64_t seq;
    memcpy(&seq, data, sizeof(seq));
    gDtorLog.push_back(seq);
}

void mooMarkGcStep(lua_State* L, int gc)
{
    (void)L;
    if (gc >= 0 && (gDtorLog.empty() || gDtorLog.back() != 0))
        gDtorLog.push_back(0);
}

// newud(size): a tagged userdata whose destructor comes from the tag; newudi(size): one with its own
// destructor. Each holds its creation number in its first 8 bytes.
int mooNewUserdata(lua_State* L)
{
    int size = luaL_checkinteger(L, 1);
    void* data = lua_newuserdatatagged(L, size_t(size), kDtorTag);
    uint64_t seq = moo_objectseq(L, -1);
    memcpy(data, &seq, sizeof(seq));
    gUserdataMade++;
    return 1;
}

int mooNewUserdataInline(lua_State* L)
{
    int size = luaL_checkinteger(L, 1);
    void* data = lua_newuserdatadtor(L, size_t(size), mooLogDtor);
    uint64_t seq = moo_objectseq(L, -1);
    memcpy(data, &seq, sizeof(seq));
    gUserdataMade++;
    return 1;
}

// Userdata of five sizes (so five GC page size classes), dropped in an order that is not the
// creation order, under allocation churn, then the rest at lua_close.
const char* const kDtorChurn = R"(
    local keep = {}
    local sizes = {8, 40, 200, 1000, 3000}
    for round = 1, 60 do
        for i = 1, 30 do
            local u
            if i % 2 == 0 then u = newud(sizes[i % 5 + 1]) else u = newudi(sizes[(i * 3) % 5 + 1]) end
            keep[#keep + 1] = u
        end
        for i = #keep, 1, -1 do
            if math.random(1, 2) == 1 then table.remove(keep, i) end
        end
        local junk = {}
        for j = 1, 200 do junk[j] = {j} end
    end
    return #keep
)";

// Runs kDtorChurn in a fresh VM, closes it, and returns the destructor log.
std::vector<uint64_t> mooDtorRun(int* made)
{
    gDtorLog.clear();
    gUserdataMade = 0;
    {
        MooState s;
        lua_setuserdatadtor(s.L, kDtorTag, mooLogDtor);
        lua_callbacks(s.L)->interrupt = mooMarkGcStep;
        lua_pushcfunction(s.L, mooNewUserdata, "newud");
        lua_setglobal(s.L, "newud");
        lua_pushcfunction(s.L, mooNewUserdataInline, "newudi");
        lua_setglobal(s.L, "newudi");
        std::string kept = mooRun(s.L, kDtorChurn);
        CHECK(kept.find("error") == std::string::npos);
        lua_callbacks(s.L)->interrupt = nullptr;
        gDtorLog.push_back(0); // lua_close is the last batch
    }
    *made = gUserdataMade;
    return gDtorLog;
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

#ifdef LUAU_CONFORMANCE_SOURCE_DIR
// DESIGN.md "Layer 3 as built": each script in tests/conformance/moo/spec pins the rules of one subject,
// beside its C++ cases in tests/moo/<subject>.test.cpp; the
// moo_test helpers let it see the raw Q48.16 value, the compiler's bytecode and luau_load's result.
TEST_CASE("MooSpecScripts")
{
    std::filesystem::path directory = std::filesystem::path(LUAU_CONFORMANCE_SOURCE_DIR) / "moo" / "spec";

    std::vector<std::string> names;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory))
        if (entry.is_regular_file() && entry.path().extension() == ".luau")
            names.push_back(entry.path().filename().string());

    REQUIRE_MESSAGE(!names.empty(), "no scripts in " << directory);
    std::sort(names.begin(), names.end());

    for (const std::string& name : names)
    {
        MooState s;
        mooOpenTestHelpers(s.L);

        std::ifstream file(directory / name, std::ios::binary);
        REQUIRE_MESSAGE(file.good(), "cannot read " << name);
        std::stringstream source;
        source << file.rdbuf();

        std::string result = mooRun(s.L, source.str().c_str());
        CHECK_MESSAGE(result == "OK", name << ": " << result);
    }
}
#endif

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
    // moo: re-pinned when layer 3 removed the vector and integer libraries: fewer startup objects shift every creation number.
    // moo: re-pinned when layer 3b added the vec3 tag: its type-name string shifts every creation number by one
    // (docs/history/layer3b/moo-v2-l3b-vec3-object.md, decision 1, item 3), and again when the vec3 library added its table
    // and function names (docs/history/layer3b/moo-v2-l3b-vec3-ops.md), and again for the quat tag and library
    // (docs/history/layer3b/moo-v2-l3b-quat.md).
    CHECK(order == "3,34,60,17,14,54,9,16,49,56,59,51,6,22,41,37,21,50,47,52,36,58,32,57,4,35,53,29,44,20,25,45,46,8,48,40,55,31,27,28,18,7,42,26,24,43,30");
}

// DETERMINISM.md 6: tostring prints the creation number, never the address.
TEST_CASE("TostringPrintsCreationNumber")
{
    MooState s;

    std::string pair = mooRun(s.L, "local a, b = {}, {} return tostring(a) .. ' ' .. tostring(b)");
    // moo: re-pinned #400/#401 -> #375/#376 when layer 3 removed the vector and integer libraries (fewer startup objects).
    // moo: re-pinned #375/#376 -> #376/#377 when layer 3b added the vec3 type-name string, then #376/#377 -> #391/#392
    // when the vec3 library added its table and names (docs/history/layer3b/moo-v2-l3b-vec3-ops.md).
    // moo: re-pinned #391/#392 -> #401/#402 when layer 3b added the quat tag and library (docs/history/layer3b/moo-v2-l3b-quat.md).
    CHECK(pair == "table: #415 table: #416");

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
    lua_pushinteger(s.L, 1);
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
    // moo: re-pinned when number became Q48.16, and again when layer 3 removed the vector and integer libraries
    // (fewer startup objects shift every creation number).
    // moo: re-pinned when layer 3b added the vec3 type-name string (docs/history/layer3b/moo-v2-l3b-vec3-object.md).
    // moo: re-pinned for the quat tag and library (docs/history/layer3b/moo-v2-l3b-quat.md).
    CHECK(mooHash(trace) == 109304034735195062ull);
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

// DETERMINISM.md 13: destructors run at the end of the GC step that found the userdata dead, in
// creation-number order, and once each; lua_close runs the rest the same way (tla/DestructorQueue.tla).
TEST_CASE("DestructorsRunInCreationOrder")
{
    int made = 0;
    std::vector<uint64_t> log = mooDtorRun(&made);

    std::vector<uint64_t> seen;
    int batches = 0;
    size_t longest = 0;
    size_t run = 0;
    uint64_t last = 0;
    for (uint64_t seq : log)
    {
        if (seq == 0)
        {
            batches += run > 0;
            longest = run > longest ? run : longest;
            run = 0;
            last = 0;
            continue;
        }
        CHECK_MESSAGE(seq > last, "destructor of #" << seq << " ran after #" << last << " in one GC step");
        last = seq;
        run++;
        seen.push_back(seq);
    }

    // each once
    CHECK(int(seen.size()) == made);
    std::sort(seen.begin(), seen.end());
    CHECK(std::adjacent_find(seen.begin(), seen.end()) == seen.end());

    // not vacuous: the GC ran destructors in several steps before lua_close, several at a time
    CHECK(batches > 3);
    CHECK(longest > 10);

    int madeAgain = 0;
    CHECK(log == mooDtorRun(&madeAgain));

    std::string text;
    for (uint64_t seq : log)
        text += std::to_string(seq) + ",";
    // Pinned: a change here changes when and in what order every host's destructors run.
    // moo: re-pinned when layer 3 removed the vector and integer libraries: fewer startup objects shift every creation number.
    // moo: re-pinned when layer 3b added the vec3 type-name string (docs/history/layer3b/moo-v2-l3b-vec3-object.md).
    // moo: re-pinned for the quat tag and library (docs/history/layer3b/moo-v2-l3b-quat.md).
    CHECK(mooHash(text) == 12678846546305343080ull);
}

// DETERMINISM.md 13: which weak entries clear, and the order of what is left, under churn with object keys,
// weak keys, weak values and both. Deterministic once pacing (4) and object hashing (1) are.
TEST_CASE("WeakTableOrderIsAFunctionOfTheProgram")
{
    const char* const weak = R"(
        local id = setmetatable({}, {__mode = "k"})
        local wk = setmetatable({}, {__mode = "k"})
        local wv = setmetatable({}, {__mode = "v"})
        local wkv = setmetatable({}, {__mode = "kv"})
        local hold = {}
        local trace = {}
        local n = 0
        for round = 1, 120 do
            for i = 1, 60 do
                n += 1
                local k = (i % 3 == 0) and function() return n end or {n}
                id[k] = n
                wk[k] = {n}
                wv[n] = k
                wkv[k] = k
                if i % 7 == 0 then hold[#hold + 1] = k end
            end
            if #hold > 40 then table.remove(hold, 1) end
            if round % 10 == 0 then
                local line = {}
                for k in pairs(wk) do line[#line + 1] = id[k] end
                local c = 0
                for _ in pairs(wv) do c += 1 end
                for _ in pairs(wkv) do c += 1 end
                trace[#trace + 1] = table.concat(line, " ") .. "/" .. c
            end
        end
        return table.concat(trace, ";")
    )";

    MooState a;
    std::string trace = mooRun(a.L, weak);
    REQUIRE(trace.find("error") == std::string::npos);

    MooState b;
    CHECK(trace == mooRun(b.L, weak));

    // Pinned.
    // moo: re-pinned when layer 3 removed the vector and integer libraries: fewer startup objects shift every creation number.
    // moo: re-pinned when layer 3b added the vec3 type-name string (docs/history/layer3b/moo-v2-l3b-vec3-object.md).
    // moo: re-pinned for the quat tag and library (docs/history/layer3b/moo-v2-l3b-quat.md).
    CHECK(mooHash(trace) == 14301579112442529628ull);
}

// DESIGN.md "Semantics": number is built on the vendored fixed, compiled with FIX_SATURATE.
TEST_CASE("FixedIsVendoredSaturating")
{
    CHECK(fixMul(3 * FIX_ONE / 2, 5 * FIX_ONE / 2) == 15 * FIX_ONE / 4);
    CHECK(fixMul(FIX_MAX, 2 * FIX_ONE) == FIX_MAX);
    CHECK(fixMul(FIX_MAX, -2 * FIX_ONE) == FIX_MIN);
    CHECK(fixDiv(FIX_ONE, 4 * FIX_ONE) == FIX_ONE / 4);
}

// DESIGN.md "Exact integers", tla/NumericFor.tla: the trip count is computed exactly before the loop starts
TEST_CASE("ForTripCountIsExact")
{
    MooState s;
    // 0.1 is 6554/65536, so the tenth step passes 1: ten iterations, the last index 0.8999...
    CHECK(mooRun(s.L, "local c = 0 for i = 0, 1, 0.1 do c += 1 end return c") == "10");
    CHECK(mooRun(s.L, "local c = 0 for i = 1, 10 do c += 1 end return c") == "10");
    CHECK(mooRun(s.L, "local c = 0 for i = 10, 1, -3 do c += 1 end return c") == "4");
    CHECK(mooRun(s.L, "local c = 0 for i = 1, 0 do c += 1 end return c") == "0");
    // at the edge of the range the index never steps past the limit, so it never saturates into one more iteration
    CHECK(mooRun(s.L, "local c, last = 0, 0 for i = math.huge - 0.5, math.huge, 1 do c += 1 last = i end return c .. ' ' .. tostring(last == math.huge - 0.5)") == "1 true");
    CHECK(mooRun(s.L, "local c = 0 for i = -math.huge, -math.huge + 3, 1 do c += 1 end return c") == "4");
    // a zero step is a named error, never an endless loop
    CHECK(mooRun(s.L, "for i = 1, 10, 0 do end return 1").find("'for' step is zero") != std::string::npos);
    CHECK(mooRun(s.L, "local z = 0 for i = 1, 10, z do end return 1").find("'for' step is zero") != std::string::npos);
    // the loop variable sees each index exactly
    CHECK(mooRun(s.L, "local t = {} for i = 0.5, 2, 0.5 do t[#t + 1] = i end return table.concat(t, ',')") == "0.5,1,1.5,2");
}

TEST_CASE("ForTripsFormula")
{
    CHECK(moo_numfortrips(0, 65536, 6554) == 10);
    CHECK(moo_numfortrips(-INT64_MAX, INT64_MAX, 1) == UINT64_MAX);
    CHECK(moo_numfortrips(INT64_MAX, -INT64_MAX, -1) == UINT64_MAX);
    CHECK(moo_numfortrips(INT64_MAX - 32768, INT64_MAX, 65536) == 1);
    CHECK(moo_numfortrips(5, 4, 1) == 0);
    CHECK(moo_numfortrips(4, 5, -1) == 0);
    CHECK(moo_numfortrips(7, 7, -INT64_MAX) == 1);
}

// The release version (VM/include/moo.h, CHANGELOG.md): the string and its parts agree, and _VERSION stays Luau's.
TEST_CASE("MooVersion")
{
    char parts[32];
    snprintf(parts, sizeof(parts), "%d.%d.%d", MOO_VERSION_MAJOR, MOO_VERSION_MINOR, MOO_VERSION_PATCH);
    CHECK(std::string(MOO_VERSION) == parts);
    CHECK(std::string(MOO_VERSION) == "1.2.1");
    CHECK(std::string(MOO_LUAU_VERSION) == "0.741");

    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    CHECK(mooRun(L, "return _VERSION") == "Luau");
    lua_close(L);
}

TEST_SUITE_END();
