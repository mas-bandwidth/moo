// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// tempreuse: expression-temporary reuse for vec3, quat and transform (docs/DESIGN.md "Expression-temporary reuse";
// tla/TempReuse.tla). The compiler marks an arithmetic op whose operand is an expression temporary (MooTempReuse.h);
// the VM writes the result into that object when it is of the result's type and at or above the fence
// (moo_tempreuse.h). These cases pin that it fires (objects created, counted by the creation counter, which is exact
// and reads no clock), that it never touches a value the program can see, that every fence site moves the fence,
// and that results are bit-identical with reuse off (-O0 marks nothing) and on (-O1, -O2) over 1024 seeded programs.
// The expected-behaviour script is tests/conformance/moo/spec/tempreuse.luau.

#include "lua.h"
#include "lualib.h"
#include "luacode.h"
#include "moo.h"

#include "Luau/BytecodeBuilder.h"
#include "Luau/Compiler.h"

#include "MooHost.h"
#include "doctest.h"

#include "lstate.h"
#include "lvm.h"
#include "moo_tempreuse.h"
#include "moo_vec3.h"

#include <stdint.h>
#include <string.h>

#include <string>
#include <vector>

TEST_SUITE_BEGIN("tempreuse");

namespace
{

struct State
{
    lua_State* L;

    State()
    {
        L = luaL_newstate();
        REQUIRE(L != nullptr);
        luaL_openlibs(L);
        mooOpenTestHelpers(L);
    }

    ~State()
    {
        lua_close(L);
    }
};

std::string compileAt(const std::string& source, int level)
{
    Luau::CompileOptions options;
    options.optimizationLevel = level;
    return Luau::compile(source, options);
}

std::string dumpAt(const std::string& source, int level)
{
    Luau::CompileOptions options;
    options.optimizationLevel = level;
    Luau::BytecodeBuilder bcb;
    bcb.setDumpFlags(Luau::BytecodeBuilder::Dump_Code);
    Luau::compileOrThrow(bcb, source, options);
    return bcb.dumpEverything();
}

// runs a chunk compiled at level that returns a function, and leaves that function on the stack
void loadFunction(lua_State* L, const std::string& source, int level)
{
    std::string bytecode = compileAt(source, level);
    REQUIRE(luau_load(L, "=tempreuse", bytecode.data(), bytecode.size(), 0) == 0);
    REQUIRE(lua_pcall(L, 0, 1, 0) == 0);
    REQUIRE(lua_isfunction(L, -1));
}

uint64_t created(lua_State* L)
{
    return L->global->gcseq;
}

fixed_t num(int64_t whole)
{
    return fixed_t(whole) * 65536;
}

// a value as text with its raw components, so two values compare bit for bit
std::string describe(lua_State* L, int idx)
{
    lua_Number c[7];
    std::string out;
    if (moo_tovec3(L, idx, c))
        out = "vec3";
    else if (moo_toquat(L, idx, c))
        out = "quat";
    else if (moo_totransform(L, idx, c))
        out = "transform";
    else if (lua_type(L, idx) == LUA_TNUMBER)
        return "number " + std::to_string(int64_t(lua_tonumber(L, idx)));
    else
        return std::string(luaL_typename(L, idx)) + " " + (lua_isstring(L, idx) ? lua_tostring(L, idx) : "");

    int n = out == "vec3" ? 3 : out == "quat" ? 4 : 7;
    for (int i = 0; i < n; ++i)
        out += " " + std::to_string(int64_t(c[i]));
    return out;
}

// splitmix64 with a fixed seed: the same programs and operands on every platform, no clock and no rand()
struct SplitMix
{
    uint64_t state;

    uint64_t next()
    {
        uint64_t z = (state += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }

    unsigned below(unsigned n)
    {
        return unsigned(next() % n);
    }

    // mostly raw values in [-8, 8) (nested products stay in range); one in 256 anywhere in the domain, so saturation
    // (and, checked, its error) is covered
    fixed_t component()
    {
        if (below(256) == 0)
            return fixed_t(next() >> 1) * (below(2) ? 1 : -1);
        return fixed_t(next() & ((uint64_t(1) << 20) - 1)) - fixed_t(uint64_t(1) << 19);
    }
};

} // namespace

// docs/DESIGN.md "Expression-temporary reuse": acc + a*s - b + b*2 makes five objects without reuse (two products,
// three sums) and two with it: a*s and b*2 are new, and each of the three sums writes into the temporary it reads.
// Counted with the creation counter (one per object) and totalbytes with the collector stopped (40 bytes per vec3).
TEST_CASE("TempReuseCutsTheAllocationsOfAnExpression")
{
    const char* source = "return function(acc, a, b, s) return acc + a*s - b + b*2 end";

    std::string results[3];
    for (int level = 0; level <= 2; ++level)
    {
        CAPTURE(level);
        State st;
        lua_State* L = st.L;
        loadFunction(L, source, level);
        moo_pushvec3(L, num(1), num(2), num(3));
        moo_pushvec3(L, num(-4), num(5), num(6));
        moo_pushvec3(L, num(7), num(-8), num(9));
        lua_pushnumber(L, num(3));

        lua_gc(L, LUA_GCSTOP, 0);
        uint64_t before = created(L);
        size_t bytesBefore = lua_totalbytes(L, 0);
        REQUIRE(lua_pcall(L, 4, 1, 0) == 0);
        uint64_t objects = created(L) - before;
        size_t bytes = lua_totalbytes(L, 0) - bytesBefore;

        CHECK(objects == (level == 0 ? 5u : 2u));
        CHECK(bytes == objects * sizeof(MooVec3));
        results[level] = describe(L, -1);
        // 1 + -12 - 7 + 14, 2 + 15 + 8 - 16, 3 + 18 - 9 + 18
        CHECK(results[level] == "vec3 " + std::to_string(num(-4)) + " " + std::to_string(num(9)) + " " + std::to_string(num(30)));
    }

    CHECK(results[0] == results[1]);
    CHECK(results[0] == results[2]);

    // -O1 marks the three sums and leaves the products, which read only parameters, unmarked; -O0 marks nothing
    std::string o1 = dumpAt(source, 1);
    CHECK(o1.find("ADDTC") != std::string::npos);
    CHECK(o1.find("SUBTB") != std::string::npos);
    CHECK(o1.find("ADDTB") != std::string::npos);
    CHECK(o1.find("MULK R") != std::string::npos);
    std::string o0 = dumpAt(source, 0);
    for (const char* name : {"ADDTB", "ADDTC", "SUBTB", "SUBTC", "MULTB", "MULTC", "MULKTB", "MINUST"})
        CHECK(o0.find(name) == std::string::npos);
}

// One expression per opcode, each with its object count off and on, so every opcode is emitted, spelled by the dump,
// and run on the reuse path; the results are equal at every level.
TEST_CASE("TempReuseEveryOpcode")
{
    struct Case
    {
        const char* body;
        const char* opcode;
        unsigned off;
        unsigned on;
    };
    // a, b: vec3; s: number; p, q: quat; t, u: transform
    const Case cases[] = {
        {"return (a*s) + b", "ADDTB", 2, 1},
        {"return b + (a*s)", "ADDTC", 2, 1},
        {"return (a*s) - b", "SUBTB", 2, 1},
        {"return b - (a*s)", "SUBTC", 2, 1},
        {"return (a*s) * b", "MULTB", 2, 1},
        {"return s * (a*s)", "MULTC", 2, 1},
        {"return (a+b) * 3", "MULKTB", 2, 1},
        {"return -(a+b)", "MINUST", 2, 1},
        {"return p * q * p * q", "MULTB", 3, 1},
        {"return t * u * t", "MULTB", 2, 1},
        // a quat temporary cannot hold the vec3 result, and a transform temporary cannot either: no reuse
        {"return (p * q) * a", "MULTB", 2, 2},
        {"return (t * u) * a", "MULTB", 2, 2},
        // the vec3 temporary on the right holds the vec3 result
        {"return p * (a + b)", "MULTC", 2, 1},
        {"return t * (a + b)", "MULTC", 2, 1},
        // compound assignment: the variable is B, the temporary C
        {"local r = a; r += b * s; r -= b * s; r *= b * s; return r", "ADDTC", 6, 3},
    };

    for (const Case& c : cases)
    {
        CAPTURE(c.body);
        std::string source = std::string("return function(a, b, s, p, q, t, u) ") + c.body + " end";
        CHECK(dumpAt(source, 1).find(c.opcode) != std::string::npos);

        std::string results[3];
        for (int level = 0; level <= 2; ++level)
        {
            CAPTURE(level);
            State st;
            lua_State* L = st.L;
            loadFunction(L, source, level);
            moo_pushvec3(L, num(1), num(2), num(3));
            moo_pushvec3(L, num(-4), num(5), num(6));
            lua_pushnumber(L, num(3));
            moo_pushquat(L, 0, 0, 46341, 46341); // about 90 degrees about z
            moo_pushquat(L, 46341, 0, 0, 46341); // about 90 degrees about x
            moo_pushtransform(L, num(1), num(0), num(0), 0, 0, 46341, 46341);
            moo_pushtransform(L, num(0), num(2), num(0), 46341, 0, 0, 46341);

            uint64_t before = created(L);
            REQUIRE(lua_pcall(L, 7, 1, 0) == 0);
            CHECK(created(L) - before == (level == 0 ? c.off : c.on));
            results[level] = describe(L, -1);
        }
        CHECK(results[0] == results[1]);
        CHECK(results[0] == results[2]);
    }
}

// The fence, directly: an object created at or after it is overwritten in place (ra gets the same object, and nothing
// is created); after moo_tempfence the same call creates a new object and leaves the old one unchanged. This is the
// reversed witness of the VM's comparison (tla/TempReuse.tla, plant "nocheck").
TEST_CASE("TempReuseFenceDecidesInPlace")
{
    State st;
    lua_State* L = st.L;

    moo_pushvec3(L, num(1), num(2), num(3)); // the temporary, index 1
    moo_pushvec3(L, num(10), num(20), num(30)); // the other operand, index 2
    lua_pushnil(L); // the result, index 3

    TValue* rt = L->base;
    TValue* rc = L->base + 1;
    TValue* ra = L->base + 2;
    GCObject* object = gcvalue(rt);

    uint64_t before = created(L);
    luaV_doarithtemp<TM_ADD>(L, ra, rt, rc, rt);
    CHECK(created(L) == before);
    CHECK(gcvalue(ra) == object);
    CHECK(describe(L, 3) == "vec3 " + std::to_string(num(11)) + " " + std::to_string(num(22)) + " " + std::to_string(num(33)));

    moo_tempfence(L);
    CHECK(L->global->mootempfence > object->gch.gcseq);
    luaV_doarithtemp<TM_ADD>(L, ra, rt, rc, rt);
    CHECK(created(L) == before + 1);
    CHECK(gcvalue(ra) != object);
    CHECK(describe(L, 3) == "vec3 " + std::to_string(num(21)) + " " + std::to_string(num(42)) + " " + std::to_string(num(63)));
    CHECK(describe(L, 1) == "vec3 " + std::to_string(num(11)) + " " + std::to_string(num(22)) + " " + std::to_string(num(33)));

    // not the result's type: a number in the marked register is never written, and the op is the ordinary one
    lua_pushnumber(L, num(2)); // index 4
    TValue* rn = L->base + 3;
    luaV_doarithtemp<TM_MUL>(L, ra, rc, rn, rn);
    CHECK(created(L) == before + 2);
    CHECK(describe(L, 4) == "number " + std::to_string(num(2)));
}

namespace fence_sites
{

uint64_t seenSeq = 0;
int calls = 0;

void record(lua_State* L)
{
    // what a host could have read is everything that exists when it returns
    seenSeq = L->global->gcseq;
    calls++;
}

int tmAdd(lua_State* L)
{
    moo_pushvec3(L, num(1), num(1), num(1)); // the metamethod makes an object and returns it
    record(L);
    return 1;
}

void interrupt(lua_State* L, int gc)
{
    (void)gc;
    record(L);
}

void step(lua_State* L, lua_Debug* ar)
{
    (void)ar;
    record(L);
}

void dtor(lua_State* L, void* data)
{
    (void)data;
    record(L);
}

void preresume(lua_State* L)
{
    record(L);
}

} // namespace fence_sites

// docs/DESIGN.md "Expression-temporary reuse", the fence sites: each place where code outside the arithmetic can run
// moves the fence past every object that exists when it returns. Each site is driven once and the fence is checked
// against the creation counter the callback saw.
TEST_CASE("TempReuseEveryFenceSiteMovesTheFence")
{
    using namespace fence_sites;

    // a Lua-called C metamethod on a table (callTMres) and on a userdata (luaV_callTM, the userdata fast path)
    for (int userdata = 0; userdata <= 1; ++userdata)
    {
        CAPTURE(userdata);
        State st;
        lua_State* L = st.L;
        if (userdata)
            lua_newuserdata(L, 8);
        else
            lua_newtable(L);
        lua_newtable(L);
        lua_pushcfunction(L, tmAdd, "__add");
        lua_setfield(L, -2, "__add");
        lua_setmetatable(L, -2);
        lua_setglobal(L, "x");
        calls = 0;
        // a register operand, so LOP_ADD and its userdata fast path run, not LOP_ADDK
        CHECK(mooRun(L, "local y = tonumber('1') local r = x + y return 0") == "0");
        CHECK(calls == 1);
        CHECK(L->global->mootempfence >= seenSeq);
        CHECK(seenSeq > 1);
    }

    // the interrupt (VM_INTERRUPT) and a GC step's interrupt (GC_INTERRUPT)
    {
        State st;
        lua_State* L = st.L;
        lua_callbacks(L)->interrupt = interrupt;
        calls = 0;
        CHECK(mooRun(L, "local t = {} for i = 1, 100 do t[i] = vec3.new(i, i, i) end return 0") == "0");
        CHECK(calls > 0);
        CHECK(L->global->mootempfence >= seenSeq);
        calls = 0;
        lua_gc(L, LUA_GCSTEP, 1); // luaC_step, whose GC_INTERRUPT fires at its start
        lua_callbacks(L)->interrupt = nullptr;
        CHECK(calls > 0);
        CHECK(L->global->mootempfence >= seenSeq);
    }

    // a debug hook (luau_callhook), in single-step mode
    {
        State st;
        lua_State* L = st.L;
        lua_callbacks(L)->debugstep = step;
        lua_singlestep(L, 1);
        calls = 0;
        CHECK(mooRun(L, "local v = vec3.new(1, 2, 3) return 0") == "0");
        lua_singlestep(L, 0);
        lua_callbacks(L)->debugstep = nullptr;
        CHECK(calls > 0);
        CHECK(L->global->mootempfence >= seenSeq);
    }

    // the destructor queue (rundtorqueue)
    {
        State st;
        lua_State* L = st.L;
        lua_newuserdatadtor(L, 8, dtor);
        lua_pop(L, 1);
        calls = 0;
        lua_gc(L, LUA_GCCOLLECT, 0);
        CHECK(calls == 1);
        moo_pushvec3(L, 1, 2, 3); // an object after the queue ran is above the fence; the fence did not move since
        lua_pop(L, 1);
        CHECK(L->global->mootempfence >= seenSeq);
        CHECK(L->global->mootempfence < L->global->gcseq);
    }

    // a resume (lua_resume): whatever the host did to the suspended thread is behind the fence
    {
        State st;
        lua_State* L = st.L;
        lua_callbacks(L)->preresume = preresume;
        calls = 0;
        CHECK(mooRun(L, "local co = coroutine.create(function() coroutine.yield(vec3.new(1, 2, 3)) end) "
                        "coroutine.resume(co) coroutine.resume(co) return 0") == "0");
        lua_callbacks(L)->preresume = nullptr;
        CHECK(calls == 2);
        CHECK(L->global->mootempfence >= seenSeq);
    }
}

namespace onallocate_host
{

int armed = 0, ref = 0, busy = 0;

// a host that, on an allocation inside the running frame, finds the live temporary a*s = (2, 4, 6) by stack index and
// keeps it (the cold review's counterexample before onallocate moved the fence)
void onallocate(lua_State* L, void* block, size_t osize, size_t nsize, uint8_t memcat, int tt, int tag)
{
    (void)block, (void)osize, (void)nsize, (void)memcat, (void)tag;
    if (!armed || ref || busy || tt != LUA_TVEC3)
        return;
    busy = 1;
    for (int i = 1; i <= lua_gettop(L); i++)
    {
        lua_Number v[3];
        if (moo_tovec3(L, i, v) && v[0] == num(2) && v[1] == num(4) && v[2] == num(6))
        {
            ref = lua_ref(L, i);
            break;
        }
    }
    busy = 0;
}

int arm(lua_State* L)
{
    (void)L;
    armed = 1;
    return 0;
}

int disarm(lua_State* L)
{
    (void)L;
    armed = 0;
    return 0;
}

} // namespace onallocate_host

// onallocate runs inside an allocation with the running frame's registers in reach: it moves the fence too, so the
// temporary the host kept is not overwritten by the next marked op
TEST_CASE("TempReuseOnallocateHostKeepsATemporary")
{
    using namespace onallocate_host;
    const char* source = "local a = vec3.new(1, 2, 3) local b = vec3.new(10, 20, 30) local s = 2\n"
                         "arm() local r = (a * s) + (b * s) disarm() return r";

    for (int level = 0; level <= 2; ++level)
    {
        CAPTURE(level);
        State st;
        lua_State* L = st.L;
        lua_pushcfunction(L, arm, "arm");
        lua_setglobal(L, "arm");
        lua_pushcfunction(L, disarm, "disarm");
        lua_setglobal(L, "disarm");
        // free registry slots first, so the host's lua_ref allocates nothing
        int refs[16];
        for (int i = 0; i < 16; i++)
        {
            lua_pushinteger(L, i);
            refs[i] = lua_ref(L, -1);
            lua_pop(L, 1);
        }
        for (int i = 0; i < 16; i++)
            lua_unref(L, refs[i]);

        armed = 0, ref = 0, busy = 0;
        lua_callbacks(L)->onallocate = onallocate;
        std::string bytecode = compileAt(source, level);
        REQUIRE(luau_load(L, "=onallocate", bytecode.data(), bytecode.size(), 0) == 0);
        REQUIRE(lua_pcall(L, 0, 1, 0) == 0);
        lua_callbacks(L)->onallocate = nullptr;

        CHECK(describe(L, -1) == "vec3 " + std::to_string(num(22)) + " " + std::to_string(num(44)) + " " + std::to_string(num(66)));
        REQUIRE(ref != 0);
        lua_getref(L, ref);
        CHECK(describe(L, -1) == "vec3 " + std::to_string(num(2)) + " " + std::to_string(num(4)) + " " + std::to_string(num(6)));
    }
}

// A metamethod's result is never overwritten in place: here __add hands back its own vec3 operand (the temporary a*s)
// after keeping it, and the marked SUBTB that reads the result must not write into it. The same with __mul returning
// a vec3 the program holds. Run at -O1 and -O2, where the ops are marked; the kept value must stay as it was.
TEST_CASE("TempReuseNeverWritesAMetamethodResult")
{
    const char* source = R"(
        return function(a, s, b)
            local kept
            local mt = {}
            mt.__add = function(x, y) if type(x) == "vec3" then kept = x return x end kept = y return y end
            local obj = setmetatable({}, mt)
            local r = (a*s + obj) - b
            local held = vec3.new(7, 8, 9)
            local mt2 = {__mul = function() return held end}
            local obj2 = setmetatable({}, mt2)
            local r2 = (obj2 * 2) + b
            return kept, r, held, r2
        end
    )";
    for (int level = 0; level <= 2; ++level)
    {
        CAPTURE(level);
        State st;
        lua_State* L = st.L;
        loadFunction(L, source, level);
        moo_pushvec3(L, num(1), num(2), num(3));
        lua_pushnumber(L, num(2));
        moo_pushvec3(L, num(1), num(1), num(1));
        REQUIRE(lua_pcall(L, 3, 4, 0) == 0);
        CHECK(describe(L, -4) == "vec3 " + std::to_string(num(2)) + " " + std::to_string(num(4)) + " " + std::to_string(num(6)));
        CHECK(describe(L, -3) == "vec3 " + std::to_string(num(1)) + " " + std::to_string(num(3)) + " " + std::to_string(num(5)));
        CHECK(describe(L, -2) == "vec3 " + std::to_string(num(7)) + " " + std::to_string(num(8)) + " " + std::to_string(num(9)));
        CHECK(describe(L, -1) == "vec3 " + std::to_string(num(8)) + " " + std::to_string(num(9)) + " " + std::to_string(num(10)));
    }
    CHECK(dumpAt(source, 1).find("SUBTB") != std::string::npos);
}

namespace generated
{

// a seeded program of typed vec3, quat, transform and number expressions, built from every marked and unmarked form
struct Generator
{
    SplitMix rng;

    std::string leaf(char type)
    {
        switch (type)
        {
        case 'v':
            return "v" + std::to_string(1 + rng.below(3));
        case 'q':
            return "q" + std::to_string(1 + rng.below(2));
        case 't':
            return "t" + std::to_string(1 + rng.below(2));
        default:
        {
            static const char* numbers[] = {"n1", "n2", "2", "0.5", "(-3)", "(n1 :: number)"};
            return numbers[rng.below(6)];
        }
        }
    }

    std::string expr(char type, int depth)
    {
        if (depth == 0 || rng.below(5) == 0)
            return leaf(type);

        int d = depth - 1;
        switch (type)
        {
        case 'v':
            switch (rng.below(12))
            {
            case 0:
                return "(" + expr('v', d) + " + " + expr('v', d) + ")";
            case 1:
                return "(" + expr('v', d) + " - " + expr('v', d) + ")";
            case 2:
                return "(" + expr('v', d) + " * " + expr('v', d) + ")";
            case 3:
                return "(" + expr('v', d) + " * " + expr('n', d) + ")";
            case 4:
                return "(" + expr('n', d) + " * " + expr('v', d) + ")";
            case 5:
                return "(" + expr('v', d) + " * 2)";
            case 6:
                return "(-" + expr('v', d) + ")";
            case 7:
                return "(" + expr('q', d) + " * " + expr('v', d) + ")";
            case 8:
                return "(" + expr('t', d) + " * " + expr('v', d) + ")";
            case 9:
                return "((" + expr('v', d) + " + " + expr('v', d) + ") :: vec3)";
            case 10:
                return "id(" + expr('v', d) + ")";
            default:
                return expr('v', d) + " + " + expr('v', d) + " - " + expr('v', d);
            }
        case 'q':
            return "(" + expr('q', d) + " * " + expr('q', d) + ")";
        case 't':
            return "(" + expr('t', d) + " * " + expr('t', d) + ")";
        default:
            switch (rng.below(4))
            {
            case 0:
                return "(" + expr('n', d) + " + " + expr('n', d) + ")";
            case 1:
                return "(" + expr('n', d) + " - " + expr('n', d) + ")";
            case 2:
                return "(" + expr('n', d) + " * " + expr('n', d) + ")";
            default:
                return "(-" + expr('n', d) + ")";
            }
        }
    }

    // locals, an upvalue, a table field, a call argument and a compound assignment around the expressions; every
    // visible value is returned, so a write into one of them shows as a difference from -O0
    std::string program()
    {
        std::string s = "return function(v1, v2, v3, q1, q2, t1, t2, n1, n2)\n";
        s += "  local function id(x) kept[#kept + 1] = x return x end\n";
        s += "  local x = " + expr('v', 4) + "\n";
        s += "  local f = function() return x end\n";
        s += "  local box = {f = " + expr('v', 3) + "}\n";
        s += "  local y = x + " + expr('v', 4) + "\n";
        s += "  local z = box.f - " + expr('v', 3) + "\n";
        s += "  y += " + expr('v', 3) + "\n";
        s += "  local w = " + expr('q', 3) + " * " + expr('v', 3) + "\n";
        s += "  local m = " + expr('t', 2) + " * (" + expr('v', 3) + ")\n";
        s += "  local k = " + expr('n', 3) + "\n";
        s += "  return x, f(), box.f, y, z, w, m, k, " + expr('v', 5) + ", v1, v2, v3, q1, t1\n";
        s += "end\n";
        return s;
    }
};

struct Outcome
{
    std::string text;
    uint64_t objects = 0;
};

Outcome run(const std::string& source, int level, uint64_t seed)
{
    State st;
    lua_State* L = st.L;
    lua_newtable(L);
    lua_setglobal(L, "kept");
    loadFunction(L, source, level);

    SplitMix values{seed};
    for (int i = 0; i < 3; ++i)
        moo_pushvec3(L, values.component(), values.component(), values.component());
    for (int i = 0; i < 2; ++i)
        moo_pushquat(L, values.component(), values.component(), values.component(), values.component());
    for (int i = 0; i < 2; ++i)
        moo_pushtransform(L, values.component(), values.component(), values.component(), values.component(), values.component(),
            values.component(), values.component());
    lua_pushnumber(L, values.component());
    lua_pushnumber(L, values.component());

    Outcome out;
    int top = lua_gettop(L) - 10;
    uint64_t before = created(L);
    int status = lua_pcall(L, 9, LUA_MULTRET, 0);
    out.objects = created(L) - before;
    if (status != 0)
    {
        out.text = std::string("error: ") + lua_tostring(L, -1);
        return out;
    }
    for (int i = top + 1; i <= lua_gettop(L); ++i)
        out.text += describe(L, i) + "\n";

    // the values the program kept through id() are visible too
    lua_getglobal(L, "kept");
    int n = int(lua_objlen(L, -1));
    for (int i = 1; i <= n; ++i)
    {
        lua_rawgeti(L, -1, i);
        out.text += "kept " + describe(L, -1) + "\n";
        lua_pop(L, 1);
    }
    return out;
}

} // namespace generated

// "error: tempreuse:6: number overflow in '+'" without "tempreuse:6: "
std::string withoutPosition(const std::string& text)
{
    if (text.rfind("error: tempreuse:", 0) != 0)
        return text;
    size_t colon = text.find(": ", strlen("error: tempreuse:"));
    return colon == std::string::npos ? text : "error: " + text.substr(colon + 2);
}

// docs/DESIGN.md "Expression-temporary reuse": 1024 seeded programs give bit-identical results, errors included, with
// reuse off (-O0) and on (-O1, -O2), and reuse never creates more objects than -O0 does.
TEST_CASE("TempReuseBitIdentical1024")
{
    generated::Generator gen{{0x6d6f6f74656d70ull}}; // "mootemp"
    uint64_t offObjects = 0, onObjects = 0, marked = 0;
    int errors = 0;

    for (int i = 0; i < 1024; ++i)
    {
        std::string source = gen.program();
        CAPTURE(i);
        CAPTURE(source);

        generated::Outcome off = generated::run(source, 0, 1000 + uint64_t(i));
        generated::Outcome o1 = generated::run(source, 1, 1000 + uint64_t(i));
        generated::Outcome o2 = generated::run(source, 2, 1000 + uint64_t(i));

        REQUIRE(off.text == o1.text);
        // -O2 inlines id(), and an error raised by an instruction next to inlined code may carry the inlined line
        // (upstream's line info, with or without reuse), so its error is compared without the position
        REQUIRE(withoutPosition(off.text) == withoutPosition(o2.text));
        CHECK(o1.objects <= off.objects);
        offObjects += off.objects;
        onObjects += o1.objects;
        if (off.text.rfind("error: ", 0) == 0)
            errors++;

        std::string dump = dumpAt(source, 1);
        for (const char* name : {"ADDTB", "ADDTC", "SUBTB", "SUBTC", "MULTB", "MULTC", "MULKTB", "MINUST"})
            for (size_t at = dump.find(name); at != std::string::npos; at = dump.find(name, at + 1))
                marked++;
    }

    MESSAGE("tempreuse 1024 programs: objects -O0 " << offObjects << ", -O1 " << onObjects << ", marked ops " << marked << ", errors " << errors);
    CHECK(onObjects < offObjects);
    CHECK(marked > 0);
}

TEST_SUITE_END();
