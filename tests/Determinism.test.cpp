// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// The determinism suite (docs/DETERMINISM.md). Each program in tests/determinism runs in a fresh VM
// with a fixed host seed and host functions that log (log, newud, newudi and the userdata
// destructors). Two hashes are taken:
//
// - effects: every log line, every destructor run (with GC step boundaries) and lua_close's
//   destructors, in order;
// - state: the full VM state just before lua_close: every GC object in page order with its type,
//   creation number, mark bits and contents (table slots, strings, userdata, closures, threads),
//   and the global counters (bytes requested, GC threshold and state, the next creation number).
//
// Both must equal tests/determinism/expected.txt on every platform and compiler. Set
// MOO_DETERMINISM_PRINT=1 to print the lines of that file.
#include "lua.h"
#include "lualib.h"
#include "luacode.h"
#include "moo.h"

#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "ltable.h"

#include "doctest.h"

#include <stdlib.h>
#include <string.h>

#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace
{

struct Fnv
{
    uint64_t h = 0xcbf29ce484222325ull;

    void bytes(const void* p, size_t n)
    {
        const unsigned char* c = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i)
            h = (h ^ c[i]) * 0x100000001b3ull;
    }

    void u64(uint64_t v)
    {
        bytes(&v, sizeof(v));
    }

    void str(const std::string& s)
    {
        u64(s.size());
        bytes(s.data(), s.size());
    }
};

Fnv gEffects;
lua_State* gHashState = nullptr;
const int kTag = 9;

void effect(const std::string& line)
{
    gEffects.str(line);
}

void dtorLog(lua_State* L, void* data)
{
    (void)L;
    uint64_t seq;
    memcpy(&seq, data, sizeof(seq));
    effect("dtor " + std::to_string(seq));
}

void stepMark(lua_State* L, int gc)
{
    (void)L;
    if (gc >= 0)
        effect("gcstep " + std::to_string(gc));
}

int hostLog(lua_State* L)
{
    std::string line = "log";
    int n = lua_gettop(L);
    for (int i = 1; i <= n; ++i)
    {
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        line += "\t" + std::string(s, len);
        lua_pop(L, 1);
    }
    effect(line);
    return 0;
}

int hostNewud(lua_State* L)
{
    void* data = lua_newuserdatatagged(L, size_t(luaL_checkinteger(L, 1)), kTag);
    uint64_t seq = moo_objectseq(L, -1);
    memcpy(data, &seq, sizeof(seq));
    return 1;
}

int hostNewudi(lua_State* L)
{
    void* data = lua_newuserdatadtor(L, size_t(luaL_checkinteger(L, 1)), dtorLog);
    uint64_t seq = moo_objectseq(L, -1);
    memcpy(data, &seq, sizeof(seq));
    return 1;
}

void hashValue(Fnv& f, const TValue* v)
{
    f.u64(uint64_t(v->tt));
    switch (v->tt)
    {
    case LUA_TNIL:
        break;
    case LUA_TBOOLEAN:
        f.u64(uint64_t(bvalue(v)));
        break;
    case LUA_TNUMBER:
    {
        double d = nvalue(v);
        f.bytes(&d, sizeof(d));
        break;
    }
    case LUA_TVECTOR:
        f.bytes(vvalue(v), sizeof(float) * LUA_VECTOR_SIZE);
        break;
    case LUA_TLIGHTUSERDATA:
        break; // a host input; the suite passes none
    case LUA_TDEADKEY:
        break; // its pointer is to a freed object, so only the tag is state
    default:
        if (iscollectable(v))
            f.u64(gcvalue(v)->gch.gcseq);
        else
            f.bytes(&v->value, sizeof(v->value));
        break;
    }
}

// Pass 1, mid-cycle: every block's header. A dead object not yet swept may hold bytes nothing wrote
// (a string buffer's unused capacity, a slot pointing at a freed key), so only its header is state.
bool hashHeader(void* context, lua_Page* page, GCObject* o)
{
    (void)page;
    Fnv& f = *static_cast<Fnv*>(context);
    f.u64(o->gch.tt);
    f.u64(o->gch.gcseq);
    f.u64(o->gch.marked);
    return false;
}

// Pass 2, after a full collection, when every object left is live: headers and contents.
bool hashObject(void* context, lua_Page* page, GCObject* o)
{
    hashHeader(context, page, o);
    Fnv& f = *static_cast<Fnv*>(context);

    switch (o->gch.tt)
    {
    case LUA_TSTRING:
        f.u64(gco2ts(o)->len);
        f.bytes(getstr(gco2ts(o)), gco2ts(o)->len);
        break;
    case LUA_TTABLE:
    {
        LuaTable* t = gco2h(o);
        f.u64(uint64_t(t->sizearray));
        f.u64(uint64_t(t->lsizenode));
        f.u64(t->metatable ? t->metatable->gcseq : 0);
        for (int i = 0; i < t->sizearray; ++i)
            hashValue(f, &t->array[i]);
        for (int i = 0; i < sizenode(t); ++i)
        {
            LuaNode* n = gnode(t, i);
            TValue key;
            getnodekey(gHashState, &key, n);
            // an empty slot's key may still point at a freed object (it becomes a dead key when the GC
            // next traverses the table), so only a live slot's key is state
            if (ttisnil(gval(n)))
                f.u64(uint64_t(key.tt));
            else
                hashValue(f, &key);
            hashValue(f, gval(n));
            f.u64(uint64_t(int64_t(gnext(n))));
        }
        break;
    }
    case LUA_TUSERDATA:
        f.u64(gco2u(o)->tag);
        f.u64(uint64_t(gco2u(o)->len));
        break;
    case LUA_TBUFFER:
        f.u64(gco2buf(o)->len);
        f.bytes(gco2buf(o)->data, gco2buf(o)->len);
        break;
    case LUA_TVEC3: // moo: a vec3's payload is state (docs/layer3b/moo-v2-l3b-vec3-object.md, card gc item 10)
        f.u64(uint64_t(gco2vec3(o)->x));
        f.u64(uint64_t(gco2vec3(o)->y));
        f.u64(uint64_t(gco2vec3(o)->z));
        break;
    case LUA_TQUAT: // moo: a quat's payload is state (docs/layer3b/moo-v2-l3b-quat-object.md)
        f.u64(uint64_t(gco2quat(o)->v.x));
        f.u64(uint64_t(gco2quat(o)->v.y));
        f.u64(uint64_t(gco2quat(o)->v.z));
        f.u64(uint64_t(gco2quat(o)->s));
        break;
    case LUA_TFUNCTION:
    {
        Closure* c = gco2cl(o);
        f.u64(c->isC);
        f.u64(c->nupvalues);
        if (!c->isC)
            f.u64(c->l.p->gcseq);
        break;
    }
    case LUA_TTHREAD:
    {
        lua_State* th = gco2th(o);
        f.u64(th->status);
        f.u64(uint64_t(th->top - th->stack));
        f.u64(uint64_t(th->ci - th->base_ci));
        break;
    }
    default:
        break;
    }
    return false;
}

struct Result
{
    uint64_t effects;
    uint64_t state;
};

Result runProgram(const std::string& name)
{
    std::string path = std::string(LUAU_CONFORMANCE_SOURCE_DIR) + "/../determinism/" + name;
    std::ifstream file(path, std::ios::binary);
    std::stringstream source;
    source << file.rdbuf();
    REQUIRE_MESSAGE(file.good(), "cannot read " << path);
    std::string text = source.str();

    gEffects = Fnv();
    Fnv state;

    lua_State* L = luaL_newstate();
    moo_setrandomseed(L, 12345);
    luaL_openlibs(L);
    lua_setuserdatadtor(L, kTag, dtorLog);
    lua_pushcfunction(L, hostLog, "log");
    lua_setglobal(L, "log");
    lua_pushcfunction(L, hostNewud, "newud");
    lua_setglobal(L, "newud");
    lua_pushcfunction(L, hostNewudi, "newudi");
    lua_setglobal(L, "newudi");
    luaL_sandbox(L);
    lua_callbacks(L)->interrupt = stepMark;

    lua_State* T = lua_newthread(L);
    luaL_sandboxthread(T);

    size_t size = 0;
    char* bytecode = luau_compile(text.data(), text.size(), nullptr, &size);
    int status = luau_load(T, name.c_str(), bytecode, size, 0);
    free(bytecode);
    if (status == 0)
        status = lua_resume(T, nullptr, 0);
    CHECK_MESSAGE(status == 0, name << ": " << (lua_isstring(T, -1) ? lua_tostring(T, -1) : "?"));
    effect("status " + std::to_string(status));

    lua_callbacks(L)->interrupt = nullptr;

    global_State* g = L->global;
    gHashState = L;
    luaM_visitgco(L, &state, hashHeader);
    state.u64(g->totalbytes);
    state.u64(g->GCthreshold);
    state.u64(g->gcstate);
    state.u64(g->gcseq);

    lua_gc(L, LUA_GCCOLLECT, 0); // its destructors are effects
    effect("collected");
    luaM_visitgco(L, &state, hashObject);
    state.u64(g->totalbytes);
    state.u64(g->GCthreshold);
    state.u64(g->gcstate);

    lua_close(L);
    effect("closed");

    return {gEffects.h, state.h};
}

std::string hex(uint64_t v)
{
    char buf[19];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)v);
    return buf;
}

} // namespace

TEST_SUITE_BEGIN("moo");

TEST_CASE("DeterminismSuite")
{
    std::string dir = std::string(LUAU_CONFORMANCE_SOURCE_DIR) + "/../determinism/";
    std::ifstream expectedFile(dir + "expected.txt");
    REQUIRE_MESSAGE(expectedFile.good(), "cannot read " << dir << "expected.txt");

    std::map<std::string, std::pair<std::string, std::string>> expected;
    std::string line;
    while (std::getline(expectedFile, line))
    {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream fields(line);
        std::string name, effects, state;
        fields >> name >> effects >> state;
        expected[name] = {effects, state};
    }
    REQUIRE(!expected.empty());

    bool print = getenv("MOO_DETERMINISM_PRINT") != nullptr;
    Fnv all;

    for (const auto& entry : expected)
    {
        const std::string& name = entry.first;
        const std::pair<std::string, std::string>& want = entry.second;
        Result r = runProgram(name);
        Result again = runProgram(name);
        CHECK_MESSAGE(r.effects == again.effects, name << ": effects differ between two runs in one process");
        CHECK_MESSAGE(r.state == again.state, name << ": state differs between two runs in one process");

        if (print)
            printf("%s %s %s\n", name.c_str(), hex(r.effects).c_str(), hex(r.state).c_str());

        CHECK_MESSAGE(hex(r.effects) == want.first, name << ": effects hash " << hex(r.effects));
        CHECK_MESSAGE(hex(r.state) == want.second, name << ": state hash " << hex(r.state));
        all.u64(r.effects);
        all.u64(r.state);
    }

    if (print)
        printf("# suite %s\n", hex(all.h).c_str());
}

TEST_SUITE_END();
