// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
// This code is based on Lua 5.x implementation licensed under MIT License; see lua_LICENSE.txt for details
#include "lobject.h"
#include "moo.h"

#include "lstate.h"
#include "lstring.h"
#include "lgc.h"
#include "ldo.h"
#include "lnumutils.h"
#include "lmem.h"          // moo: luaM_newgcofixed
#include "moo_format.h"    // moo: moo_numtostr
#include "moo_vec3.h"      // moo: vec3
#include "moo_quat.h"      // moo: quat
#include "moo_transform.h" // moo: transform

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

const TValue luaO_nilobject_ = {{NULL}, {0}, LUA_TNIL};

int luaO_log2(unsigned int x)
{
    static const uint8_t log_2[256] = {0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6,
                                       6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
                                       7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
                                       7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
                                       8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
                                       8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
                                       8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8};
    int l = -1;
    while (x >= 256)
    {
        l += 8;
        x >>= 8;
    }
    return l + log_2[x];
}

// moo: vec3, quat and transform allocation, equality and tostring (docs/history/layer3b/moo-v2-l3b-vec3-object.md)
MooVec3* moo_vec3_new(lua_State* L, fixed_t x, fixed_t y, fixed_t z)
{
    MooVec3* v = luaM_newgcofixed(L, MooVec3, sizeof(MooVec3), L->activememcat, LUA_TVEC3);
    luaC_init(L, v, LUA_TVEC3);
    v->x = x;
    v->y = y;
    v->z = z;
    return v;
}

int moo_vec3_equal(const MooVec3* a, const MooVec3* b)
{
    return a->x == b->x && a->y == b->y && a->z == b->z;
}

char* moo_vec3_tostring(char* buf, const MooVec3* v)
{
    memcpy(buf, "vec3(", 5);
    char* p = moo_numtostr(buf + 5, v->x);
    *p++ = ',';
    *p++ = ' ';
    p = moo_numtostr(p, v->y);
    *p++ = ',';
    *p++ = ' ';
    p = moo_numtostr(p, v->z);
    *p++ = ')';
    return p;
}

// moo: quat
MooQuat* moo_quat_new(lua_State* L, fixed_t x, fixed_t y, fixed_t z, fixed_t s)
{
    MooQuat* q = luaM_newgcofixed(L, MooQuat, sizeof(MooQuat), L->activememcat, LUA_TQUAT);
    luaC_init(L, q, LUA_TQUAT);
    q->v.x = x;
    q->v.y = y;
    q->v.z = z;
    q->s = s;
    return q;
}

int moo_quat_equal(const MooQuat* a, const MooQuat* b)
{
    return a->v.x == b->v.x && a->v.y == b->v.y && a->v.z == b->v.z && a->s == b->s;
}

char* moo_quat_tostring(char* buf, const MooQuat* q)
{
    memcpy(buf, "quat(", 5);
    char* p = moo_numtostr(buf + 5, q->v.x);
    *p++ = ',';
    *p++ = ' ';
    p = moo_numtostr(p, q->v.y);
    *p++ = ',';
    *p++ = ' ';
    p = moo_numtostr(p, q->v.z);
    *p++ = ',';
    *p++ = ' ';
    p = moo_numtostr(p, q->s);
    *p++ = ')';
    return p;
}

// moo: transform
MooTransform* moo_transform_new(lua_State* L, fixTransform t)
{
    MooTransform* o = luaM_newgcofixed(L, MooTransform, sizeof(MooTransform), L->activememcat, LUA_TTRANSFORM);
    luaC_init(L, o, LUA_TTRANSFORM);
    o->p = t.p;
    o->q = t.q;
    return o;
}

int moo_transform_equal(const MooTransform* a, const MooTransform* b)
{
    return a->p.x == b->p.x && a->p.y == b->p.y && a->p.z == b->p.z && a->q.v.x == b->q.v.x && a->q.v.y == b->q.v.y &&
           a->q.v.z == b->q.v.z && a->q.s == b->q.s;
}

char* moo_transform_tostring(char* buf, const MooTransform* t)
{
    const fixed_t c[7] = {t->p.x, t->p.y, t->p.z, t->q.v.x, t->q.v.y, t->q.v.z, t->q.s};
    memcpy(buf, "transform(vec3(", 15);
    char* p = buf + 15;
    for (int i = 0; i < 7; ++i)
    {
        if (i == 3)
        {
            memcpy(p, "), quat(", 8);
            p += 8;
        }
        else if (i != 0)
        {
            *p++ = ',';
            *p++ = ' ';
        }
        p = moo_numtostr(p, c[i]);
    }
    *p++ = ')';
    *p++ = ')';
    return p;
}

// moo: no float vector arm; vec3, quat and transform compare by value (docs/history/layer3b/moo-v2-l3b-vec3-object.md,
// decision 3)
int luaO_rawequalObj(const TValue* t1, const TValue* t2)
{
    if (ttype(t1) != ttype(t2))
        return 0;
    else
        switch (ttype(t1))
        {
        case LUA_TNIL:
            return 1;
        case LUA_TNUMBER:
            return luai_numeq(nvalue(t1), nvalue(t2));
        case LUA_TINTEGER:
            return luai_inteq(lvalue(t1), lvalue(t2));
        case LUA_TBOOLEAN:
            return bvalue(t1) == bvalue(t2); // boolean true must be 1 !!
        case LUA_TLIGHTUSERDATA:
            return pvalue(t1) == pvalue(t2) && lightuserdatatag(t1) == lightuserdatatag(t2);
        case LUA_TVEC3:
            return moo_vec3_equal(vec3value(t1), vec3value(t2));
        case LUA_TQUAT:
            return moo_quat_equal(quatvalue(t1), quatvalue(t2));
        case LUA_TTRANSFORM:
            return moo_transform_equal(transformvalue(t1), transformvalue(t2));
        default:
            LUAU_ASSERT(iscollectable(t1));
            return gcvalue(t1) == gcvalue(t2);
        }
}

int luaO_rawequalKey(const TKey* t1, const TValue* t2)
{
    if (ttype(t1) != ttype(t2))
        return 0;
    else
        switch (ttype(t1))
        {
        case LUA_TNIL:
            return 1;
        case LUA_TNUMBER:
            return luai_numeq(nvalue(t1), nvalue(t2));
        case LUA_TINTEGER:
            return luai_inteq(lvalue(t1), lvalue(t2));
        case LUA_TBOOLEAN:
            return bvalue(t1) == bvalue(t2); // boolean true must be 1 !!
        case LUA_TLIGHTUSERDATA:
            return pvalue(t1) == pvalue(t2) && lightuserdatatag(t1) == lightuserdatatag(t2);
        case LUA_TVEC3:
            return moo_vec3_equal(vec3value(t1), vec3value(t2));
        case LUA_TQUAT:
            return moo_quat_equal(quatvalue(t1), quatvalue(t2));
        case LUA_TTRANSFORM:
            return moo_transform_equal(transformvalue(t1), transformvalue(t2));
        default:
            LUAU_ASSERT(iscollectable(t1));
            return gcvalue(t1) == gcvalue(t2);
        }
}

// moo: tonumber and string coercion use the literal parser (moo_parse.h). Spaces around it and one sign are
// allowed; out of range is not a number.
int luaO_str2d(const char* s, lua_Number* result)
{
    while (isspace(cast_to(unsigned char, *s)))
        s++;
    bool neg = false;
    if (*s == '-' || *s == '+')
        neg = *s++ == '-';
    const char* end = s;
    while (*end && !isspace(cast_to(unsigned char, *end)))
        end++;
    const char* rest = end;
    while (isspace(cast_to(unsigned char, *rest)))
        rest++;
    size_t len = size_t(end - s);
    if (*rest != '\0' || len == 0 || len > MOO_PARSE_MAX_DIGITS + 32)
        return 0;
    char buf[MOO_PARSE_MAX_DIGITS + 33];
    memcpy(buf, s, len);
    buf[len] = '\0';
    fixed_t v = 0;
    int status = moo_numparse(buf, &v);
    if (status == MOO_PARSE_MALFORMED || status == MOO_PARSE_OUT_OF_RANGE)
        return 0;
    *result = neg ? -v : v;
    return 1;
}

int luaO_str2l(const char* s, int64_t* result, int base)
{
    char* endptr = nullptr;
    if (base == 10)
    {
        *result = luai_str2long(s, &endptr, base);
        if (endptr == s)
            return 0;                         // conversion failed
        if (*endptr == 'x' || *endptr == 'X') // maybe an hexadecimal constant?
            *result = (int64_t)strtoull(s, &endptr, 16);
    }
    else
    {
        // unsigned parse in other bases
        *result = (int64_t)strtoull(s, &endptr, base);
        if (endptr == s)
            return 0;
    }
    if (*endptr == '\0')
        return 1; // most common case
    while (isspace(cast_to(unsigned char, *endptr)))
        endptr++;
    if (*endptr != '\0')
        return 0; // invalid trailing characters?
    return 1;
}

const char* luaO_pushvfstring(lua_State* L, const char* fmt, va_list argp)
{
    char result[LUA_BUFFERSIZE];
    vsnprintf(result, sizeof(result), fmt, argp);

    setsvalue(L, L->top, luaS_new(L, result));
    incr_top(L);
    return svalue(L->top - 1);
}

const char* luaO_pushfstring(lua_State* L, const char* fmt, ...)
{
    const char* msg;
    va_list argp;
    va_start(argp, fmt);
    msg = luaO_pushvfstring(L, fmt, argp);
    va_end(argp);
    return msg;
}

// Possible chunkname prefixes:
//
// '=' prefix: meant to represent custom chunknames. When truncation is needed,
// the beginning of the chunkname is kept.
//
// '@' prefix: meant to represent filepaths. When truncation is needed, the end
// of the filepath is kept, as this is more useful for identifying the file.
const char* luaO_chunkid(char* buf, size_t buflen, const char* source, size_t srclen)
{
    if (*source == '=')
    {
        if (srclen <= buflen)
            return source + 1;
        // truncate the part after =
        memcpy(buf, source + 1, buflen - 1);
        buf[buflen - 1] = '\0';
    }
    else if (*source == '@')
    {
        if (srclen <= buflen)
            return source + 1;
        // truncate the part after @
        memcpy(buf, "...", 3);
        memcpy(buf + 3, source + srclen - (buflen - 4), buflen - 4);
        buf[buflen - 1] = '\0';
    }
    else
    {                                         // buf = [string "string"]
        size_t len = strcspn(source, "\n\r"); // stop at first newline
        buflen -= sizeof("[string \"...\"]");
        if (len > buflen)
            len = buflen;
        strcpy(buf, "[string \"");
        if (source[len] != '\0')
        { // must truncate?
            strncat(buf, source, len);
            strcat(buf, "...");
        }
        else
            strcat(buf, source);
        strcat(buf, "\"]");
    }
    return buf;
}
