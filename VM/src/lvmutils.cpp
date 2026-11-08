// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
// This code is based on Lua 5.x implementation licensed under MIT License; see lua_LICENSE.txt for details
#include "lclass.h"
#include "lfunc.h"
#include "lvm.h"

#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "lgc.h"
#include "ldo.h"
#include "lnumutils.h"
#include "moo_vec3.h"      // moo: vec3, quat and transform operators
#include "moo_quat.h"      // moo: quat
#include "moo_transform.h" // moo: transform
#include "moo_vec2.h"      // moo: layer 3c narrow types (docs/DESIGN.md "Layer 3c as built")
#include "moo_mat3.h"
#include "moo_plane.h"
#include "moo_aabb.h"
#include "moo_fix30.h"
#include "moo_wide.h" // moo: layer 3d wide and time (docs/DESIGN.md "Layer 3d as built")
#include "moo_tempreuse.h" // moo: expression-temporary reuse

#include <string.h>

// limit for table tag-method chains (to avoid loops)
#define MAXTAGLOOP 100

LUAU_FASTFLAG(LuauFrozenMetaButterfly)
LUAU_FASTFLAG(DebugLuauUserDefinedClassesRuntime)
LUAU_FASTFLAG(LuauPromoteProto)
LUAU_FASTFLAGVARIABLE(LuauCallLuauTm)

const TValue* luaV_tonumber(const TValue* obj, TValue* n)
{
    lua_Number num;
    if (ttisnumber(obj))
        return obj;
    if (ttisstring(obj) && luaO_str2d(svalue(obj), &num))
    {
        setnvalue(n, num);
        return n;
    }
    else
        return NULL;
}

int luaV_tostring(lua_State* L, StkId obj)
{
    if (!ttisnumber(obj))
        return 0;
    else
    {
        char s[LUAI_MAXNUM2STR];
        lua_Number n = nvalue(obj);
        char* e = luai_fixed2str(s, n);
        LUAU_ASSERT(e < s + sizeof(s));
        setsvalue(L, obj, luaS_newlstr(L, s, e - s));
        return 1;
    }
}

static LUAU_FORCEINLINE void callTMluau(lua_State* L, StkId top, Closure* ccl, int nresults)
{
    if (++L->nCcalls >= LUAI_MAXCCALLS)
        luaD_checkCstack(L);

    ptrdiff_t funcoffset = savestack(L, top);
    Proto* p = getproto(ccl);

    CallInfo* ci = incr_ci(L);
    ci->func = top;
    ci->p = p;
    ci->base = top + 1;
    ci->top = L->top + ccl->stacksize;
    ci->savedpc = p->code;
    ci->flags = LUA_CALLINFO_RETURN;
    ci->nresults = nresults;

    L->base = ci->base;

    luaD_checkstackfornewci(L, ccl->stacksize); // clobbers 'top'
    LUAU_ASSERT(ci->top <= L->stack_last);

    StkId argi = L->top;
    StkId argend = L->base + p->numparams;
    while (argi < argend)
        setnilvalue(argi++);
    L->top = p->is_vararg ? argi : ci->top;

    if (p->exectarget != 0 && p->execdata)
        ci->flags |= LUA_CALLINFO_NATIVE;

    luau_execute(L);

    // resume_continue is not handled here as metamethods are not yieldable

    L->top = restorestack(L, funcoffset) + nresults;
    L->nCcalls--;
}

static StkId callTMres(lua_State* L, StkId res, const TValue* f, const TValue* p1, const TValue* p2)
{
    ptrdiff_t result = savestack(L, res);
    // using stack room beyond top is technically safe here, but for very complicated reasons:
    // * The stack guarantees EXTRA_STACK room beyond stack_last (see luaD_reallocstack) will be allocated
    // * we cannot move luaD_checkstack above because the arguments are *sometimes* pointers to the lua
    // stack and checkstack may invalidate those pointers
    // * we cannot use savestack/restorestack because the arguments are sometimes on the C++ stack
    // * during stack reallocation all of the allocated stack is copied (even beyond stack_last) so these
    // values will be preserved even if they go past stack_last
    if (FFlag::LuauCallLuauTm)
    {
        StkId top = L->top;
        LUAU_ASSERT((top + 3) < (L->stack + L->stacksize));
        setobj2s(L, top, f);      // push function
        setobj2s(L, top + 1, p1); // 1st argument
        setobj2s(L, top + 2, p2); // 2nd argument

        // fast-path for Luau to Luau calls
        if (L->isactive && isLua(L->ci) && ttisfunction(top) && !clvalue(top)->isC)
        {
            L->top += 3;
            callTMluau(L, top, clvalue(top), 1);
        }
        else
        {
            luaD_checkstack(L, 3);
            StkId func = L->top;
            L->top += 3;
            luaD_call(L, func, 1);
        }
    }
    else
    {
        LUAU_ASSERT((L->top + 3) < (L->stack + L->stacksize));
        setobj2s(L, L->top, f);      // push function
        setobj2s(L, L->top + 1, p1); // 1st argument
        setobj2s(L, L->top + 2, p2); // 2nd argument
        luaD_checkstack(L, 3);
        L->top += 3;
        luaD_call(L, L->top - 3, 1);
    }

    moo_tempfence(L); // moo: whatever the metamethod returns or kept is never overwritten in place (moo_tempreuse.h)

    res = restorestack(L, result);
    L->top--;
    setobj2s(L, res, L->top);
    return res;
}

// There is only one call location for this function, but inlining it has been measured to have a negative effect on luaV_settable
static LUAU_NOINLINE void callTM(lua_State* L, const TValue* f, const TValue* p1, const TValue* p2, const TValue* p3)
{
    // using stack room beyond top is technically safe here, but for very complicated reasons:
    // * The stack guarantees EXTRA_STACK room beyond stack_last (see luaD_reallocstack) will be allocated
    // * we cannot move luaD_checkstack above because the arguments are *sometimes* pointers to the lua
    // stack and checkstack may invalidate those pointers
    // * we cannot use savestack/restorestack because the arguments are sometimes on the C++ stack
    // * during stack reallocation all of the allocated stack is copied (even beyond stack_last) so these
    // values will be preserved even if they go past stack_last
    if (FFlag::LuauCallLuauTm)
    {
        StkId top = L->top;
        LUAU_ASSERT((top + 4) < (L->stack + L->stacksize));
        setobj2s(L, top, f);      // push function
        setobj2s(L, top + 1, p1); // 1st argument
        setobj2s(L, top + 2, p2); // 2nd argument
        setobj2s(L, top + 3, p3); // 3th argument

        // fast-path for Luau to Luau calls
        if (L->isactive && isLua(L->ci) && ttisfunction(top) && !clvalue(top)->isC)
        {
            L->top += 4;
            callTMluau(L, top, clvalue(top), 0);
        }
        else
        {
            luaD_checkstack(L, 4);
            StkId func = L->top;
            L->top += 4;
            luaD_call(L, func, 0);
        }
    }
    else
    {
        LUAU_ASSERT((L->top + 4) < (L->stack + L->stacksize));
        setobj2s(L, L->top, f);      // push function
        setobj2s(L, L->top + 1, p1); // 1st argument
        setobj2s(L, L->top + 2, p2); // 2nd argument
        setobj2s(L, L->top + 3, p3); // 3th argument
        luaD_checkstack(L, 4);
        L->top += 4;
        luaD_call(L, L->top - 4, 0);
    }
}

// moo: v.x, v.y, v.z read a vec3's components and q.x, q.y, q.z, q.w a quat's (w is fixed's s), upper case too,
// as Luau's vector does. Any other key is the index error: neither type has a metatable.
static bool moo_value_index(const TValue* t, const TValue* key, StkId val)
{
    if (!ttisstring(key) || tsvalue(key)->len != 1)
        return false;
    fixed_t x, y, z, w;
    bool hasw;
    if (ttisvec3(t))
    {
        const MooVec3* v = vec3value(t);
        x = v->x, y = v->y, z = v->z, w = 0, hasw = false;
    }
    else if (ttisquat(t))
    {
        const MooQuat* q = quatvalue(t);
        x = q->v.x, y = q->v.y, z = q->v.z, w = q->s, hasw = true;
    }
    else
        return false;
    switch (getstr(tsvalue(key))[0])
    {
    case 'x':
    case 'X':
        setnvalue(val, x);
        return true;
    case 'y':
    case 'Y':
        setnvalue(val, y);
        return true;
    case 'z':
    case 'Z':
        setnvalue(val, z);
        return true;
    case 'w':
    case 'W':
        if (!hasw)
            return false;
        setnvalue(val, w);
        return true;
    default:
        return false;
    }
}

// moo: t.p and t.q read a transform's position and rotation as new objects (docs/history/layer3b/moo-v2-l3b-transform.md,
// decision 3). No GC step here: the debt is paid at the next check, as for any allocation in an instruction.
static bool moo_transform_index(lua_State* L, const MooTransform* t, const TValue* key, StkId val)
{
    if (!ttisstring(key) || tsvalue(key)->len != 1)
        return false;
    switch (getstr(tsvalue(key))[0])
    {
    case 'p':
    {
        MooVec3* v = moo_vec3_new(L, t->p.x, t->p.y, t->p.z);
        setvec3value(L, val, v);
        return true;
    }
    case 'q':
    {
        MooQuat* q = moo_quat_new(L, t->q.v.x, t->q.v.y, t->q.v.z, t->q.s);
        setquatvalue(L, val, q);
        return true;
    }
    default:
        return false;
    }
}

void luaV_gettable(lua_State* L, const TValue* t, TValue* key, StkId val)
{
    int loop;
    for (loop = 0; loop < MAXTAGLOOP; loop++)
    {
        const TValue* tm;
        if (ttistable(t))
        { // `t' is a table?
            LuaTable* h = hvalue(t);

            const TValue* res = luaH_get(h, key); // do a primitive get

            if (res != luaO_nilobject)
                L->cachedslot = gval2slot(h, res); // remember slot to accelerate future lookups

            if (FFlag::LuauFrozenMetaButterfly)
            {
                if (LUAU_LIKELY(!ttisnil(res)))
                {
                    setobj2s(L, val, res);
                    return;
                }

                if ((tm = fasttm(L, h->metatable, TM_INDEX)) == NULL)
                {
                    setnilvalue(val);
                    return;
                }
            }
            else
            {
                if (!ttisnil(res) // result is no nil?
                    || (tm = fasttm(L, h->metatable, TM_INDEX)) == NULL)
                { // or no TM?
                    setobj2s(L, val, res);
                    return;
                }
            }

            // t isn't a table, so see if it has an INDEX meta-method to look up the key with
        }
        else if (LUAU_UNLIKELY(FFlag::DebugLuauUserDefinedClassesRuntime && ttisobject(t)))
        {
            LuauObject* inst = objectvalue(t);
            const TValue* offsettval = luaH_get(inst->lclass->memberstooffset, key);

            // Class instances throw if you try to access a member that is not
            // present.
            if (ttisnil(offsettval))
                luaG_missingmembererror(L, t, key);

            const uint32_t offset = uint32_t(moo_numtrunc(nvalue(offsettval)));
            setobj2s(L, val, luaR_lookupmemberatoffset(inst, offset));
            return;
        }
        else if (LUAU_UNLIKELY(FFlag::DebugLuauUserDefinedClassesRuntime && ttisclass(t)))
        {
            LuauClass* lco = classvalue(t);
            const TValue* res = luaH_get(lco->memberstooffset, key);

            // Class objects throw if you try to access a member that is not
            // present.
            if (ttisnil(res))
                luaG_missingmembererror(L, t, key);

            const uint32_t offset = uint32_t(moo_numtrunc(nvalue(res)));
            LUAU_ASSERT(offset < lco->numberofallmembers);

            // This is the case where we try to access an instance member on a
            // class object, for example:
            //
            //  class Box
            //      public item
            //      function print(self) print(self.item) end
            //  end
            //
            //  local _ = Box.item
            //
            if (offset < lco->numberofinstancemembers)
                luaG_missingmembererror(L, t, key);

            setobj2s(L, val, &lco->staticmembers[offset - lco->numberofinstancemembers]);
            return;
        }
        else if ((ttisvec3(t) || ttisquat(t)) && moo_value_index(t, key, val))
            return; // moo: vec3 and quat components
        else if (ttistransform(t) && moo_transform_index(L, transformvalue(t), key, val))
            return; // moo: transform
        else if (ttisnil(tm = luaT_gettmbyobj(L, t, TM_INDEX)))
            luaG_indexerror(L, t, key);
        if (ttisfunction(tm))
        {
            callTMres(L, val, tm, t, key);
            return;
        }
        t = tm; // else repeat with `tm'
    }
    luaG_runerror(L, "'__index' chain too long; possible loop");
}

void luaV_settable(lua_State* L, const TValue* t, TValue* key, StkId val)
{
    int loop;
    TValue temp;
    for (loop = 0; loop < MAXTAGLOOP; loop++)
    {
        const TValue* tm;
        if (ttistable(t))
        { // `t' is a table?
            LuaTable* h = hvalue(t);

            const TValue* oldval = luaH_get(h, key);

            // should we assign the key? (if key is valid or __newindex is not set)
            if (!ttisnil(oldval) || (tm = fasttm(L, h->metatable, TM_NEWINDEX)) == NULL)
            {
                if (h->readonly)
                    luaG_readonlyerror(L);

                // luaH_set would work but would repeat the lookup so we use luaH_setslot that can reuse oldval if it's safe
                TValue* newval = luaH_setslot(L, h, oldval, key);

                L->cachedslot = gval2slot(h, newval); // remember slot to accelerate future lookups

                setobj2t(L, newval, val);
                luaC_barriert(L, h, val);
                return;
            }

            // fallthrough to metamethod
        }
        else if (LUAU_UNLIKELY(FFlag::DebugLuauUserDefinedClassesRuntime && ttisobject(t)))
        {
            LuauObject* inst = objectvalue(t);
            const TValue* offset = luaH_get(inst->lclass->memberstooffset, key);
            if (ttisnil(offset))
                luaG_missingmembererror(L, t, key);
            const uint32_t offsetnum = uint32_t(moo_numtrunc(nvalue(offset)));
            LUAU_ASSERT(offsetnum < inst->lclass->numberofallmembers);
            if (offsetnum >= inst->lclass->numberofinstancemembers)
                luaG_indexerror(L, t, key);
            setobj2class(L, &inst->members[offsetnum], val);
            luaC_barrier(L, inst, val);
            return;
        }
        else if (ttisnil(tm = luaT_gettmbyobj(L, t, TM_NEWINDEX)))
            luaG_indexerror(L, t, key);

        if (ttisfunction(tm))
        {
            callTM(L, tm, t, key, val);
            return;
        }
        // else repeat with `tm'
        setobj(L, &temp, tm); // avoid pointing inside table (may rehash)
        t = &temp;
    }
    luaG_runerror(L, "'__newindex' chain too long; possible loop");
}

static int call_binTM(lua_State* L, const TValue* p1, const TValue* p2, StkId res, TMS event)
{
    const TValue* tm = luaT_gettmbyobj(L, p1, event); // try first operand
    if (ttisnil(tm))
        tm = luaT_gettmbyobj(L, p2, event); // try second operand
    if (ttisnil(tm))
        return 0;
    callTMres(L, res, tm, p1, p2);
    return 1;
}

static const TValue* get_compTM(lua_State* L, LuaTable* mt1, LuaTable* mt2, TMS event)
{
    const TValue* tm1 = fasttm(L, mt1, event);
    const TValue* tm2;
    if (tm1 == NULL)
        return NULL; // no metamethod
    if (mt1 == mt2)
        return tm1; // same metatables => same metamethods
    tm2 = fasttm(L, mt2, event);
    if (tm2 == NULL)
        return NULL;                // no metamethod
    if (luaO_rawequalObj(tm1, tm2)) // same metamethods?
        return tm1;
    return NULL;
}

static int call_orderTM(lua_State* L, const TValue* p1, const TValue* p2, TMS event, bool error = false)
{
    const TValue* tm1 = luaT_gettmbyobj(L, p1, event);
    const TValue* tm2;
    if (ttisnil(tm1))
    {
        if (error)
            luaG_ordererror(L, p1, p2, event);
        return -1; // no metamethod?
    }
    tm2 = luaT_gettmbyobj(L, p2, event);
    if (!luaO_rawequalObj(tm1, tm2)) // different metamethods?
    {
        if (error)
            luaG_ordererror(L, p1, p2, event);
        return -1;
    }
    callTMres(L, L->top, tm1, p1, p2);
    return !l_isfalse(L->top);
}

int luaV_strcmp(const TString* ls, const TString* rs)
{
    if (ls == rs)
        return 0;

    const char* l = getstr(ls);
    const char* r = getstr(rs);

    // always safe to read one character because even empty strings are nul terminated
    if (*l != *r)
        return uint8_t(*l) - uint8_t(*r);

    size_t ll = ls->len;
    size_t lr = rs->len;
    size_t lmin = ll < lr ? ll : lr;

    int res = memcmp(l, r, lmin);
    if (res != 0)
        return res;

    return ll == lr ? 0 : ll < lr ? -1 : 1;
}

int luaV_lessthan(lua_State* L, const TValue* l, const TValue* r)
{
    if (LUAU_UNLIKELY(ttype(l) != ttype(r)))
        luaG_ordererror(L, l, r, TM_LT);
    else if (LUAU_LIKELY(ttisnumber(l)))
        return luai_numlt(nvalue(l), nvalue(r));
    else if (ttisstring(l))
        return luaV_strcmp(tsvalue(l), tsvalue(r)) < 0;
    else if (ttiswide(l)) // moo: wide and time are ordered (fixWideLt; a time is an int64)
        return fixWideLt(moo_wide_payload(widevalue(l)), moo_wide_payload(widevalue(r)));
    else if (ttistime(l))
        return moo_time_payload(timevalue(l)) < moo_time_payload(timevalue(r));
    else
        return call_orderTM(L, l, r, TM_LT, /* error= */ true);
}

int luaV_lessequal(lua_State* L, const TValue* l, const TValue* r)
{
    int res;
    if (ttype(l) != ttype(r))
        luaG_ordererror(L, l, r, TM_LE);
    else if (ttisnumber(l))
        return luai_numle(nvalue(l), nvalue(r));
    else if (ttisstring(l))
        return luaV_strcmp(tsvalue(l), tsvalue(r)) <= 0;
    else if (ttiswide(l)) // moo: as luaV_lessthan
        return fixWideLe(moo_wide_payload(widevalue(l)), moo_wide_payload(widevalue(r)));
    else if (ttistime(l))
        return moo_time_payload(timevalue(l)) <= moo_time_payload(timevalue(r));
    else if ((res = call_orderTM(L, l, r, TM_LE)) != -1) // first try `le'
        return res;
    else if ((res = call_orderTM(L, r, l, TM_LT)) == -1) // error if not `lt'
        luaG_ordererror(L, l, r, TM_LE);
    return !res;
}

int luaV_equalval(lua_State* L, const TValue* t1, const TValue* t2)
{
    const TValue* tm;
    LUAU_ASSERT(ttype(t1) == ttype(t2));
    switch (ttype(t1))
    {
    case LUA_TNIL:
        return 1;
    case LUA_TNUMBER:
        return luai_numeq(nvalue(t1), nvalue(t2));
    case LUA_TINTEGER:
        return luai_inteq(lvalue(t1), lvalue(t2));
    case LUA_TBOOLEAN:
        return bvalue(t1) == bvalue(t2); // true must be 1 !!
    case LUA_TLIGHTUSERDATA:
        return pvalue(t1) == pvalue(t2) && lightuserdatatag(t1) == lightuserdatatag(t2);
    case LUA_TVEC3: // moo: by value
        return moo_vec3_equal(vec3value(t1), vec3value(t2));
    case LUA_TQUAT: // moo: quat
        return moo_quat_equal(quatvalue(t1), quatvalue(t2));
    case LUA_TTRANSFORM: // moo: transform
        return moo_transform_equal(transformvalue(t1), transformvalue(t2));
    case LUA_TVEC2: // moo: layer 3c narrow types (docs/DESIGN.md "Layer 3c as built")
        return moo_vec2_equal(vec2value(t1), vec2value(t2));
    case LUA_TMAT3:
        return moo_mat3_equal(mat3value(t1), mat3value(t2));
    case LUA_TPLANE:
        return moo_plane_equal(planevalue(t1), planevalue(t2));
    case LUA_TAABB:
        return moo_aabb_equal(aabbvalue(t1), aabbvalue(t2));
    case LUA_TFIX30:
        return moo_fix30_equal(fix30value(t1), fix30value(t2));
    case LUA_TWIDE: // moo: layer 3d wide and time (docs/DESIGN.md "Layer 3d as built")
        return moo_wide_equal(widevalue(t1), widevalue(t2));
    case LUA_TPOSWIDE:
        return moo_poswide_equal(poswidevalue(t1), poswidevalue(t2));
    case LUA_TWORLDTRANSFORMWIDE:
        return moo_worldtransformwide_equal(worldtransformwidevalue(t1), worldtransformwidevalue(t2));
    case LUA_TAABBWIDE:
        return moo_aabbwide_equal(aabbwidevalue(t1), aabbwidevalue(t2));
    case LUA_TTIME:
        return moo_time_equal(timevalue(t1), timevalue(t2));
    case LUA_TUSERDATA:
    {
        tm = get_compTM(L, uvalue(t1)->metatable, uvalue(t2)->metatable, TM_EQ);
        if (!tm)
            return uvalue(t1) == uvalue(t2);
        break; // will try TM
    }
    case LUA_TCLASS:
        return classvalue(t1) == classvalue(t2);
    case LUA_TOBJECT:
    {
        // We follow the same rules as metatables.
        tm = get_compTM(L, objectvalue(t1)->lclass->instancemetatable, objectvalue(t2)->lclass->instancemetatable, TM_EQ);
        if (!tm)
            return objectvalue(t1) == objectvalue(t2);
        break; // will try TM
    }
    case LUA_TTABLE:
    {
        tm = get_compTM(L, hvalue(t1)->metatable, hvalue(t2)->metatable, TM_EQ);
        if (!tm)
            return hvalue(t1) == hvalue(t2);
        break; // will try TM
    }
    default:
        return gcvalue(t1) == gcvalue(t2);
    }
    callTMres(L, L->top, tm, t1, t2); // call TM
    return !l_isfalse(L->top);
}

void luaV_concat(lua_State* L, int total, int last)
{
    do
    {
        StkId top = L->base + last + 1;
        int n = 2; // number of elements handled in this pass (at least 2)
        if (!(ttisstring(top - 2) || ttisnumber(top - 2)) || !tostring(L, top - 1))
        {
            if (!call_binTM(L, top - 2, top - 1, top - 2, TM_CONCAT))
                luaG_concaterror(L, top - 2, top - 1);
        }
        else if (tsvalue(top - 1)->len == 0) // second op is empty?
            (void)tostring(L, top - 2);      // result is first op (as string)
        else
        {
            // at least two string values; get as many as possible
            size_t tl = tsvalue(top - 1)->len;
            char* buffer;
            int i;
            // collect total length
            for (n = 1; n < total && tostring(L, top - n - 1); n++)
            {
                size_t l = tsvalue(top - n - 1)->len;
                if (l > MAXSSIZE - tl)
                    luaG_runerror(L, "string length overflow");
                tl += l;
            }

            char buf[LUA_BUFFERSIZE];
            TString* ts = nullptr;

            if (tl < LUA_BUFFERSIZE)
            {
                buffer = buf;
            }
            else
            {
                ts = luaS_bufstart(L, tl);
                buffer = ts->data;
            }

            tl = 0;
            for (i = n; i > 0; i--)
            { // concat all strings
                size_t l = tsvalue(top - i)->len;
                memcpy(buffer + tl, svalue(top - i), l);
                tl += l;
            }

            if (tl < LUA_BUFFERSIZE)
            {
                setsvalue(L, top - n, luaS_newlstr(L, buffer, tl));
            }
            else
            {
                setsvalue(L, top - n, luaS_buffinish(L, ts));
            }
        }
        total -= n - 1; // got `n' strings to create 1 new
        last -= n - 1;
    } while (total > 1); // repeat until only 1 result left
}

// moo: vec3 arithmetic is fixed's, which under FIX_SATURATE saturates per component exactly as number does
namespace
{

#if MOO_OVERFLOW_ERRORS
// a checked build raises the same error as number where release saturates
bool mooVec3AddSaturates(fixVec3 a, fixVec3 b)
{
    return moo_numaddsaturates(a.x, b.x) || moo_numaddsaturates(a.y, b.y) || moo_numaddsaturates(a.z, b.z);
}

bool mooVec3SubSaturates(fixVec3 a, fixVec3 b)
{
    return moo_numsubsaturates(a.x, b.x) || moo_numsubsaturates(a.y, b.y) || moo_numsubsaturates(a.z, b.z);
}

bool mooVec3MulSaturates(fixVec3 a, fixVec3 b)
{
    return moo_nummulsaturates(a.x, b.x) || moo_nummulsaturates(a.y, b.y) || moo_nummulsaturates(a.z, b.z);
}

bool mooVec3MulScalarSaturates(fixed_t s, fixVec3 a)
{
    return moo_nummulsaturates(s, a.x) || moo_nummulsaturates(s, a.y) || moo_nummulsaturates(s, a.z);
}
#endif

// every vec3 operation allocates a new object. The result is stored before the GC check, as LOP_NEWTABLE does: a GC
// step can reallocate the stack (shrinkstack), so ra is not valid after luaC_checkGC.
void mooVec3SetResult(lua_State* L, StkId ra, fixVec3 v)
{
    MooVec3* out = moo_vec3_new(L, v.x, v.y, v.z);
    setvec3value(L, ra, out);
    luaC_checkGC(L);
}

} // namespace

// moo: the layer 3d operators (docs/DESIGN.md "Layer 3d as built", W2). Each is one fixed function, which under
// FIX_SATURATE saturates where the exact result is out of range; a checked build raises where release saturates.
// Division by zero is named first in every build, as number's is.
namespace
{

#if MOO_OVERFLOW_ERRORS
// the checked build's tests: each is true exactly when the fixed function beside it in mooLayer3dArith saturates. Add
// and subtract compare against the defined wrap (they differ whenever the exact result is out of range, and the wrap
// of an in-range result is the result); products and quotients are tested on the exact intermediate, as fixed forms it
// (fixed_wide.h fixWideRoundProduct and fixWideDiv, fixed_time.h fixTimeMulFixed and fixTimeDivFixed).
bool mooWideAddSaturates(fixedWide_t a, fixedWide_t b)
{
    return !fixInt128Eq(fixWideAdd(a, b), fixInt128Add(a, b));
}

bool mooWideSubSaturates(fixedWide_t a, fixedWide_t b)
{
    return !fixInt128Eq(fixWideSub(a, b), fixInt128Sub(a, b));
}

bool mooWideProductSaturates(fixUInt256 product)
{
    fixUInt256 r = fixInt256Shr(fixUInt256Add(product, fixUInt256FromU64(uint64_t(FIX_HALF))), FIX_FRACTION_BITS);
    bool negative = fixInt256IsNegative(r);
    fixUInt128 extension = negative ? fixUInt128Not(FIX_UINT128_ZERO) : FIX_UINT128_ZERO;
    fixedWide_t low = fixInt128FromUnsigned(r.lo);
    return !fixUInt128Eq(r.hi, extension) || fixInt128IsNegative(low) != negative || !fixIsValidWideCoord(low);
}

// b is not zero
bool mooWideQuotientSaturates(fixedWide_t a, fixedWide_t b)
{
    fixUInt256 numerator = fixUInt256Shl(fixUInt256FromU128(fixWideMagnitude(a)), FIX_FRACTION_BITS);
    fixUInt256 quotient, remainder;
    fixUInt256DivMod(numerator, fixUInt256FromU128(fixWideMagnitude(b)), &quotient, &remainder);
    return !fixUInt128Eq(quotient.hi, FIX_UINT128_ZERO) || fixUInt128Gt(quotient.lo, fixInt128ToUnsigned(FIX_WIDE_MAX));
}

// the exact difference is outside number's range (fixWideSubToFixed narrows it to FIX_MIN or FIX_MAX)
bool mooWideDifferenceSaturates(fixedWide_t a, fixedWide_t b)
{
    fixedWide_t difference = fixInt128Sub(a, b);
    bool negative = fixInt128IsNegative(a);
    if (negative != fixInt128IsNegative(b) && negative != fixInt128IsNegative(difference))
        return true;
    return fixInt128Gt(difference, fixInt128FromI64(FIX_MAX)) || fixInt128Lt(difference, fixInt128FromI64(FIX_MIN));
}

bool mooTimeAddSaturates(fixTime t, fixTime u)
{
    return fixTimeAdd(t, u) != fixTime(uint64_t(t) + uint64_t(u));
}

bool mooTimeSubSaturates(fixTime t, fixTime u)
{
    return fixTimeSub(t, u) != fixTime(uint64_t(t) - uint64_t(u));
}

// time's range and number's are the same, +-INT64_MAX
bool mooTimeOutOfRange(fixInt128 q)
{
    return fixInt128Gt(q, fixInt128FromI64(FIX_TIME_MAX)) || fixInt128Lt(q, fixInt128FromI64(FIX_TIME_MIN));
}

bool mooTimeMulSaturates(fixTime t, fixed_t s)
{
    return mooTimeOutOfRange(
        fixInt128Shr(fixInt128Add(fixInt128MulI64(t, s), fixInt128FromI64(FIX_HALF)), FIX_FRACTION_BITS));
}

// d is not zero; the quotient of fixTimeDivFixed (d a number) and of fixTimeDiv (d a time)
bool mooTimeQuotientSaturates(fixTime t, int64_t d)
{
    return mooTimeOutOfRange(
        fixInt128Div(fixInt128ShiftLeft(fixInt128FromI64(t), FIX_FRACTION_BITS), fixInt128FromI64(d)));
}
#endif

void mooWideSetResult(lua_State* L, StkId ra, fixedWide_t w)
{
    MooFixedWide* out = moo_wide_new(L, w);
    setwidevalue(L, ra, out);
    luaC_checkGC(L); // after the store: ra is not valid after a GC step (mooVec3SetResult)
}

void mooPosWideSetResult(lua_State* L, StkId ra, fixPosWide p)
{
    MooPosWide* out = moo_poswide_new(L, p);
    setposwidevalue(L, ra, out);
    luaC_checkGC(L);
}

void mooTimeSetResult(lua_State* L, StkId ra, fixTime t)
{
    MooTime* out = moo_time_new(L, t);
    settimevalue(L, ra, out);
    luaC_checkGC(L);
}

bool mooIsLayer3d(const TValue* o)
{
    return unsigned(ttype(o) - LUA_TWIDE) <= unsigned(LUA_TTIME - LUA_TWIDE);
}

// the layer 3d operators; false for any other operand pair, which takes the ordinary path (and its error)
template<TMS op>
bool mooLayer3dArith(lua_State* L, StkId ra, const TValue* rb, const TValue* rc)
{
    if (ttiswide(rb) && (op == TM_UNM || ttiswide(rc)))
    {
        fixedWide_t a = moo_wide_payload(widevalue(rb));
        fixedWide_t b = moo_wide_payload(widevalue(rc));
        switch (op)
        {
        case TM_UNM:
            // never saturates: no wide value is the reserved minimum (fixIsValidWideCoord)
            mooWideSetResult(L, ra, fixWideNeg(a));
            return true;
        case TM_ADD:
#if MOO_OVERFLOW_ERRORS
            if (mooWideAddSaturates(a, b))
                luaG_runerror(L, "wide overflow in '+'");
#endif
            mooWideSetResult(L, ra, fixWideAdd(a, b));
            return true;
        case TM_SUB:
#if MOO_OVERFLOW_ERRORS
            if (mooWideSubSaturates(a, b))
                luaG_runerror(L, "wide overflow in '-'");
#endif
            mooWideSetResult(L, ra, fixWideSub(a, b));
            return true;
        case TM_MUL:
#if MOO_OVERFLOW_ERRORS
            if (mooWideProductSaturates(fixInt256MulI128(a, b)))
                luaG_runerror(L, "wide overflow in '*'");
#endif
            mooWideSetResult(L, ra, fixWideMul(a, b));
            return true;
        case TM_DIV:
            if (fixInt128Eq(b, FIX_INT128_ZERO))
                luaG_runerror(L, "division by zero");
#if MOO_OVERFLOW_ERRORS
            if (mooWideQuotientSaturates(a, b))
                luaG_runerror(L, "wide overflow in '/'");
#endif
            mooWideSetResult(L, ra, fixWideDiv(a, b));
            return true;
        default:
            return false;
        }
    }
    if (ttiswide(rb) && ttisnumber(rc))
    {
        fixedWide_t a = moo_wide_payload(widevalue(rb));
        fixed_t s = nvalue(rc);
        switch (op)
        {
        case TM_ADD:
        case TM_SUB:
        {
            // a number is never INT64_MIN, so -s is exact
            fixed_t d = op == TM_ADD ? s : -s;
#if MOO_OVERFLOW_ERRORS
            if (mooWideAddSaturates(a, fixInt128FromI64(d)))
                luaG_runerror(L, "wide overflow in '%s'", op == TM_ADD ? "+" : "-");
#endif
            mooWideSetResult(L, ra, fixWideOffset(a, d));
            return true;
        }
        case TM_MUL:
#if MOO_OVERFLOW_ERRORS
            if (mooWideProductSaturates(fixInt256MulI128ByI64(a, s)))
                luaG_runerror(L, "wide overflow in '*'");
#endif
            mooWideSetResult(L, ra, fixWideMulFixed(a, s));
            return true;
        case TM_DIV:
            if (s == 0)
                luaG_runerror(L, "division by zero");
#if MOO_OVERFLOW_ERRORS
            if (mooWideQuotientSaturates(a, fixWideFromFixed(s)))
                luaG_runerror(L, "wide overflow in '/'");
#endif
            mooWideSetResult(L, ra, fixWideDivFixed(a, s));
            return true;
        default:
            return false;
        }
    }
    if (op == TM_MUL && ttisnumber(rb) && ttiswide(rc))
    {
        fixed_t s = nvalue(rb);
        fixedWide_t a = moo_wide_payload(widevalue(rc));
#if MOO_OVERFLOW_ERRORS
        if (mooWideProductSaturates(fixInt256MulI128ByI64(a, s)))
            luaG_runerror(L, "wide overflow in '*'");
#endif
        mooWideSetResult(L, ra, fixWideMulFixed(a, s));
        return true;
    }
    if (ttisposwide(rb) && (op == TM_ADD || op == TM_SUB) && ttisvec3(rc))
    {
        fixPosWide p = moo_poswide_payload(poswidevalue(rb));
        fixVec3 v = moo_vec3_payload(vec3value(rc));
        // a number is never INT64_MIN, so negating the components is exact
        fixVec3 d = op == TM_ADD ? v : fixVecNeg(v);
#if MOO_OVERFLOW_ERRORS
        if (mooWideAddSaturates(p.x, fixInt128FromI64(d.x)) || mooWideAddSaturates(p.y, fixInt128FromI64(d.y)) ||
            mooWideAddSaturates(p.z, fixInt128FromI64(d.z)))
            luaG_runerror(L, "poswide overflow in '%s'", op == TM_ADD ? "+" : "-");
#endif
        mooPosWideSetResult(L, ra, fixPosWideOffset(p, d));
        return true;
    }
    if (op == TM_SUB && ttisposwide(rb) && ttisposwide(rc))
    {
        fixPosWide a = moo_poswide_payload(poswidevalue(rb));
        fixPosWide b = moo_poswide_payload(poswidevalue(rc));
#if MOO_OVERFLOW_ERRORS
        if (mooWideDifferenceSaturates(a.x, b.x) || mooWideDifferenceSaturates(a.y, b.y) ||
            mooWideDifferenceSaturates(a.z, b.z))
            luaG_runerror(L, "poswide difference out of local range");
#endif
        mooVec3SetResult(L, ra, fixPosWideSub(a, b));
        return true;
    }
    if (op == TM_MUL && ttisworldtransformwide(rb) && (ttistransform(rc) || ttisvec3(rc)))
    {
        // compounds of fixed functions, verbatim with no named error (the 3b rule)
        fixWorldTransformWide t = moo_worldtransformwide_payload(worldtransformwidevalue(rb));
        if (ttistransform(rc))
        {
            MooWorldTransformWide* out =
                moo_worldtransformwide_new(L, fixMulWorldTransformsWide(t, moo_transform_payload(transformvalue(rc))));
            setworldtransformwidevalue(L, ra, out);
            luaC_checkGC(L);
        }
        else
            mooPosWideSetResult(L, ra, fixTransformWorldPointWide(t, moo_vec3_payload(vec3value(rc))));
        return true;
    }
    if (ttistime(rb) && (op == TM_UNM || ttistime(rc)))
    {
        fixTime t = moo_time_payload(timevalue(rb));
        fixTime u = moo_time_payload(timevalue(rc));
        switch (op)
        {
        case TM_UNM:
            // never saturates: no time value is the reserved INT64_MIN
            mooTimeSetResult(L, ra, fixTimeNeg(t));
            return true;
        case TM_ADD:
#if MOO_OVERFLOW_ERRORS
            if (mooTimeAddSaturates(t, u))
                luaG_runerror(L, "time overflow in '+'");
#endif
            mooTimeSetResult(L, ra, fixTimeAdd(t, u));
            return true;
        case TM_SUB:
#if MOO_OVERFLOW_ERRORS
            if (mooTimeSubSaturates(t, u))
                luaG_runerror(L, "time overflow in '-'");
#endif
            mooTimeSetResult(L, ra, fixTimeSub(t, u));
            return true;
        case TM_DIV:
            // the ratio of two times is a number
            if (u == 0)
                luaG_runerror(L, "division by zero");
#if MOO_OVERFLOW_ERRORS
            if (mooTimeQuotientSaturates(t, u))
                luaG_runerror(L, "time overflow in '/'");
#endif
            setnvalue(ra, fixTimeDiv(t, u));
            return true;
        default:
            return false;
        }
    }
    if (ttistime(rb) && ttisnumber(rc) && (op == TM_MUL || op == TM_DIV))
    {
        fixTime t = moo_time_payload(timevalue(rb));
        fixed_t s = nvalue(rc);
        if (op == TM_MUL)
        {
#if MOO_OVERFLOW_ERRORS
            if (mooTimeMulSaturates(t, s))
                luaG_runerror(L, "time overflow in '*'");
#endif
            mooTimeSetResult(L, ra, fixTimeMulFixed(t, s));
            return true;
        }
        if (s == 0)
            luaG_runerror(L, "division by zero");
#if MOO_OVERFLOW_ERRORS
        if (mooTimeQuotientSaturates(t, s))
            luaG_runerror(L, "time overflow in '/'");
#endif
        mooTimeSetResult(L, ra, fixTimeDivFixed(t, s));
        return true;
    }
    return false;
}

} // namespace

template<TMS op>
void luaV_doarithimpl(lua_State* L, StkId ra, const TValue* rb, const TValue* rc)
{
    // moo: layer 3d (wide, poswide, worldtransformwide, aabbwide, time); one range compare when neither operand is one
    if ((mooIsLayer3d(rb) || mooIsLayer3d(rc)) && mooLayer3dArith<op>(L, ra, rb, rc))
        return;

    TValue tempb, tempc;
    const TValue *b, *c;

    // moo: vec3, quat and transform operators are fixed's functions (docs/DESIGN.md "Operations"): vec3 + - * and
    // unary -, vec3 * number, quat * quat (fixMulQuat), quat * vec3 (fixRotateVector), transform * transform
    // (fixMulTransforms) and transform * vec3 (fixTransformPoint). Any other operand falls through to the ordinary
    // arithmetic error. Every result is a new object (docs/history/layer3b/moo-v2-l3b-transform.md, decision 2).
    if (op == TM_MUL && ttistransform(rb) && (ttistransform(rc) || ttisvec3(rc)))
    {
        fixTransform a = moo_transform_payload(transformvalue(rb));
        if (ttistransform(rc))
        {
            fixTransform r = fixMulTransforms(a, moo_transform_payload(transformvalue(rc)));
            MooTransform* out = moo_transform_new(L, r);
            settransformvalue(L, ra, out);
            luaC_checkGC(L); // after the store: ra is not valid after a GC step (mooVec3SetResult)
        }
        else
            mooVec3SetResult(L, ra, fixTransformPoint(a, moo_vec3_payload(vec3value(rc))));
        return;
    }
    if (op == TM_MUL && ttisquat(rb) && (ttisquat(rc) || ttisvec3(rc)))
    {
        fixQuat q = moo_quat_payload(quatvalue(rb));
        if (ttisquat(rc))
        {
            fixQuat r = fixMulQuat(q, moo_quat_payload(quatvalue(rc)));
            MooQuat* out = moo_quat_new(L, r.v.x, r.v.y, r.v.z, r.s);
            setquatvalue(L, ra, out);
            luaC_checkGC(L); // after the store: ra is not valid after a GC step (mooVec3SetResult)
        }
        else
            mooVec3SetResult(L, ra, fixRotateVector(q, moo_vec3_payload(vec3value(rc))));
        return;
    }
    if (ttisvec3(rb) || ttisvec3(rc))
    {
        fixVec3 a = ttisvec3(rb) ? moo_vec3_payload(vec3value(rb)) : FIX_LITERAL(fixVec3){0, 0, 0};
        fixVec3 d = ttisvec3(rc) ? moo_vec3_payload(vec3value(rc)) : FIX_LITERAL(fixVec3){0, 0, 0};

        if (ttisvec3(rb) && ttisvec3(rc))
        {
            switch (op)
            {
            case TM_ADD:
#if MOO_OVERFLOW_ERRORS
                if (mooVec3AddSaturates(a, d))
                    luaG_runerror(L, "number overflow in '+'");
#endif
                mooVec3SetResult(L, ra, fixVecAdd(a, d));
                return;
            case TM_SUB:
#if MOO_OVERFLOW_ERRORS
                if (mooVec3SubSaturates(a, d))
                    luaG_runerror(L, "number overflow in '-'");
#endif
                mooVec3SetResult(L, ra, fixVecSub(a, d));
                return;
            case TM_MUL:
#if MOO_OVERFLOW_ERRORS
                if (mooVec3MulSaturates(a, d))
                    luaG_runerror(L, "number overflow in '*'");
#endif
                mooVec3SetResult(L, ra, fixVecMul(a, d));
                return;
            case TM_UNM:
                mooVec3SetResult(L, ra, fixVecNeg(a));
                return;
            default:
                break;
            }
        }
        else if (op == TM_MUL && ttisvec3(rb) && ttisnumber(rc))
        {
            fixed_t s = nvalue(rc);
#if MOO_OVERFLOW_ERRORS
            if (mooVec3MulScalarSaturates(s, a))
                luaG_runerror(L, "number overflow in '*'");
#endif
            mooVec3SetResult(L, ra, fixMulSV(s, a));
            return;
        }
        else if (op == TM_MUL && ttisnumber(rb) && ttisvec3(rc))
        {
            fixed_t s = nvalue(rb);
#if MOO_OVERFLOW_ERRORS
            if (mooVec3MulScalarSaturates(s, d))
                luaG_runerror(L, "number overflow in '*'");
#endif
            mooVec3SetResult(L, ra, fixMulSV(s, d));
            return;
        }
    }
    if ((b = luaV_tonumber(rb, &tempb)) != NULL && (c = luaV_tonumber(rc, &tempc)) != NULL)
    {
        lua_Number nb = nvalue(b), nc = nvalue(c);

        // moo: named, never infinity or NaN
        if ((op == TM_DIV || op == TM_IDIV || op == TM_MOD) && nc == 0)
            luaG_runerror(L, "division by zero");

#if MOO_OVERFLOW_ERRORS
        // moo: a checked build raises where release saturates
        if ((op == TM_ADD && luai_numaddsat(nb, nc)) || (op == TM_SUB && luai_numsubsat(nb, nc)) ||
            (op == TM_MUL && luai_nummulsat(nb, nc)) || (op == TM_DIV && luai_numdivsat(nb, nc)) ||
            (op == TM_IDIV && luai_numidivsat(nb, nc)))
            luaG_runerror(L, "number overflow in '%s'",
                op == TM_ADD ? "+" : op == TM_SUB ? "-" : op == TM_MUL ? "*" : op == TM_DIV ? "/" : "//");
#endif

        switch (op)
        {
        case TM_ADD:
            setnvalue(ra, luai_numadd(nb, nc));
            break;
        case TM_SUB:
            setnvalue(ra, luai_numsub(nb, nc));
            break;
        case TM_MUL:
            setnvalue(ra, luai_nummul(nb, nc));
            break;
        case TM_DIV:
            setnvalue(ra, luai_numdiv(nb, nc));
            break;
        case TM_IDIV:
            setnvalue(ra, luai_numidiv(nb, nc));
            break;
        case TM_MOD:
            setnvalue(ra, luai_nummod(nb, nc));
            break;
        case TM_POW:
        {
            // moo: the same value and error as math.pow
            fixed_t r = 0;
            if (const char* error = moo_numpowerror(moo_numpow(nb, nc, &r)))
                luaG_runerror(L, "%s", error);
            setnvalue(ra, r);
            break;
        }
        case TM_UNM:
            setnvalue(ra, luai_numunm(nb));
            break;
        default:
            LUAU_ASSERT(0);
            break;
        }
    }
    else
    {
        if (!call_binTM(L, rb, rc, ra, op))
        {
            luaG_aritherror(L, rb, rc, op);
        }
    }
}

// instantiate private template implementation for external callers
template void luaV_doarithimpl<TM_ADD>(lua_State* L, StkId ra, const TValue* rb, const TValue* rc);
template void luaV_doarithimpl<TM_SUB>(lua_State* L, StkId ra, const TValue* rb, const TValue* rc);
template void luaV_doarithimpl<TM_MUL>(lua_State* L, StkId ra, const TValue* rb, const TValue* rc);
template void luaV_doarithimpl<TM_DIV>(lua_State* L, StkId ra, const TValue* rb, const TValue* rc);
template void luaV_doarithimpl<TM_IDIV>(lua_State* L, StkId ra, const TValue* rb, const TValue* rc);
template void luaV_doarithimpl<TM_MOD>(lua_State* L, StkId ra, const TValue* rb, const TValue* rc);
template void luaV_doarithimpl<TM_POW>(lua_State* L, StkId ra, const TValue* rb, const TValue* rc);
template void luaV_doarithimpl<TM_UNM>(lua_State* L, StkId ra, const TValue* rb, const TValue* rc);

// moo: expression-temporary reuse (moo_tempreuse.h; docs/DESIGN.md "Expression-temporary reuse"; tla/TempReuse.tla).
// rt is the operand register the compiler marked. The operation is luaV_doarithimpl's, case for case, with the same
// fixed function and the same checked-build error; only where the result goes differs: into the object in rt when
// it is of the result's type and at or above the fence, which allocates nothing (so no GC check). Every other case,
// including every metamethod and every error, is luaV_doarithimpl itself.
namespace
{

// the object in rt when the result, of type tt, may be written into it: the type matches, it was created at or after
// the fence, and in the memory category a new object would get (a host may switch categories between two ops)
GCObject* mooTempObject(lua_State* L, const TValue* rt, int tt)
{
    if (ttype(rt) != tt)
        return NULL;

    GCObject* o = gcvalue(rt);
    return o->gch.gcseq >= L->global->mootempfence && o->gch.memcat == L->activememcat ? o : NULL;
}

void mooVec3Write(lua_State* L, StkId ra, const TValue* rt, GCObject* o, fixVec3 v)
{
    o->v3.x = v.x;
    o->v3.y = v.y;
    o->v3.z = v.z;
    setobj2s(L, ra, rt);
}

} // namespace

template<TMS op>
void luaV_doarithtemp(lua_State* L, StkId ra, const TValue* rb, const TValue* rc, const TValue* rt)
{
    if (op == TM_MUL && ttistransform(rb) && (ttistransform(rc) || ttisvec3(rc)))
    {
        fixTransform a = moo_transform_payload(transformvalue(rb));
        if (ttistransform(rc))
        {
            if (GCObject* o = mooTempObject(L, rt, LUA_TTRANSFORM))
            {
                fixTransform r = fixMulTransforms(a, moo_transform_payload(transformvalue(rc)));
                o->tf.p = r.p;
                o->tf.q = r.q;
                setobj2s(L, ra, rt);
                return;
            }
        }
        else if (GCObject* o = mooTempObject(L, rt, LUA_TVEC3))
        {
            mooVec3Write(L, ra, rt, o, fixTransformPoint(a, moo_vec3_payload(vec3value(rc))));
            return;
        }
    }
    else if (op == TM_MUL && ttisquat(rb) && (ttisquat(rc) || ttisvec3(rc)))
    {
        fixQuat q = moo_quat_payload(quatvalue(rb));
        if (ttisquat(rc))
        {
            if (GCObject* o = mooTempObject(L, rt, LUA_TQUAT))
            {
                fixQuat r = fixMulQuat(q, moo_quat_payload(quatvalue(rc)));
                o->q.v = r.v;
                o->q.s = r.s;
                setobj2s(L, ra, rt);
                return;
            }
        }
        else if (GCObject* o = mooTempObject(L, rt, LUA_TVEC3))
        {
            mooVec3Write(L, ra, rt, o, fixRotateVector(q, moo_vec3_payload(vec3value(rc))));
            return;
        }
    }
    else if (ttisvec3(rb) || ttisvec3(rc))
    {
        if (GCObject* o = mooTempObject(L, rt, LUA_TVEC3))
        {
            fixVec3 a = ttisvec3(rb) ? moo_vec3_payload(vec3value(rb)) : FIX_LITERAL(fixVec3){0, 0, 0};
            fixVec3 d = ttisvec3(rc) ? moo_vec3_payload(vec3value(rc)) : FIX_LITERAL(fixVec3){0, 0, 0};

            if (ttisvec3(rb) && ttisvec3(rc))
            {
                switch (op)
                {
                case TM_ADD:
#if MOO_OVERFLOW_ERRORS
                    if (mooVec3AddSaturates(a, d))
                        luaG_runerror(L, "number overflow in '+'");
#endif
                    mooVec3Write(L, ra, rt, o, fixVecAdd(a, d));
                    return;
                case TM_SUB:
#if MOO_OVERFLOW_ERRORS
                    if (mooVec3SubSaturates(a, d))
                        luaG_runerror(L, "number overflow in '-'");
#endif
                    mooVec3Write(L, ra, rt, o, fixVecSub(a, d));
                    return;
                case TM_MUL:
#if MOO_OVERFLOW_ERRORS
                    if (mooVec3MulSaturates(a, d))
                        luaG_runerror(L, "number overflow in '*'");
#endif
                    mooVec3Write(L, ra, rt, o, fixVecMul(a, d));
                    return;
                case TM_UNM:
                    mooVec3Write(L, ra, rt, o, fixVecNeg(a));
                    return;
                default:
                    break;
                }
            }
            else if (op == TM_MUL && ttisvec3(rb) && ttisnumber(rc))
            {
                fixed_t s = nvalue(rc);
#if MOO_OVERFLOW_ERRORS
                if (mooVec3MulScalarSaturates(s, a))
                    luaG_runerror(L, "number overflow in '*'");
#endif
                mooVec3Write(L, ra, rt, o, fixMulSV(s, a));
                return;
            }
            else if (op == TM_MUL && ttisnumber(rb) && ttisvec3(rc))
            {
                fixed_t s = nvalue(rb);
#if MOO_OVERFLOW_ERRORS
                if (mooVec3MulScalarSaturates(s, d))
                    luaG_runerror(L, "number overflow in '*'");
#endif
                mooVec3Write(L, ra, rt, o, fixMulSV(s, d));
                return;
            }
        }
    }

    luaV_doarithimpl<op>(L, ra, rb, rc);
}

template void luaV_doarithtemp<TM_ADD>(lua_State* L, StkId ra, const TValue* rb, const TValue* rc, const TValue* rt);
template void luaV_doarithtemp<TM_SUB>(lua_State* L, StkId ra, const TValue* rb, const TValue* rc, const TValue* rt);
template void luaV_doarithtemp<TM_MUL>(lua_State* L, StkId ra, const TValue* rb, const TValue* rc, const TValue* rt);
template void luaV_doarithtemp<TM_UNM>(lua_State* L, StkId ra, const TValue* rb, const TValue* rc, const TValue* rt);

void luaV_dolen(lua_State* L, StkId ra, const TValue* rb)
{
    const TValue* tm = NULL;
    switch (ttype(rb))
    {
    case LUA_TTABLE:
    {
        LuaTable* h = hvalue(rb);
        if ((tm = fasttm(L, h->metatable, TM_LEN)) == NULL)
        {
            setnvalue(ra, cast_num(luaH_getn(h)));
            return;
        }
        break;
    }
    case LUA_TSTRING:
    {
        TString* ts = tsvalue(rb);
        setnvalue(ra, cast_num(ts->len));
        return;
    }
    default:
        tm = luaT_gettmbyobj(L, rb, TM_LEN);
    }

    if (ttisnil(tm))
        luaG_typeerror(L, rb, "get length of");

    StkId res = callTMres(L, ra, tm, rb, luaO_nilobject);
    if (!ttisnumber(res))
        luaG_runerror(L, "'__len' must return a number"); // note, we can't access rb since stack may have been reallocated
}

LUAU_NOINLINE void luaV_prepareFORN(lua_State* L, StkId plimit, StkId pstep, StkId pinit)
{
    if (!ttisnumber(pinit) && !luaV_tonumber(pinit, pinit))
        luaG_forerror(L, pinit, "initial value");
    if (!ttisnumber(plimit) && !luaV_tonumber(plimit, plimit))
        luaG_forerror(L, plimit, "limit");
    if (!ttisnumber(pstep) && !luaV_tonumber(pstep, pstep))
        luaG_forerror(L, pstep, "step");
    // moo: a zero step would never end
    if (nvalue(pstep) == 0)
        luaG_runerror(L, "'for' step is zero");
}

// calls a C function f with no yielding support; optionally save one resulting value to the res register
// the function and arguments have to already be pushed to L->top
LUAU_NOINLINE void luaV_callTM(lua_State* L, int nparams, int res)
{
    ++L->nCcalls;

    if (L->nCcalls >= LUAI_MAXCCALLS)
        luaD_checkCstack(L);

    luaD_checkstack(L, LUA_MINSTACK);

    StkId top = L->top;
    StkId fun = top - nparams - 1;

    CallInfo* ci = incr_ci(L);
    ci->func = fun;
    ci->p = nullptr;
    ci->base = fun + 1;
    ci->top = top + LUA_MINSTACK;
    ci->savedpc = NULL;
    ci->flags = 0;
    ci->nresults = (res >= 0);
    LUAU_ASSERT(ci->top <= L->stack_last);

    LUAU_ASSERT(ttisfunction(ci->func));
    LUAU_ASSERT(clvalue(ci->func)->isC);

    L->base = fun + 1;
    LUAU_ASSERT(L->top == L->base + nparams);

    lua_CFunction func = clvalue(fun)->c.f;
    int n = func(L);
    LUAU_ASSERT(n >= 0); // yields should have been blocked by nCcalls

    // ci is our callinfo, cip is our parent
    // note that we read L->ci again since it may have been reallocated by the call
    CallInfo* cip = L->ci - 1;

    // copy return value into parent stack
    if (res >= 0)
    {
        if (n > 0)
        {
            setobj2s(L, &cip->base[res], L->top - n);
        }
        else
        {
            setnilvalue(&cip->base[res]);
        }
    }

    L->ci = cip;
    L->base = cip->base;
    L->top = cip->top;

    --L->nCcalls;

    moo_tempfence(L); // moo: whatever the metamethod returns or kept is never overwritten in place (moo_tempreuse.h)
}

LUAU_NOINLINE void luaV_tryfuncTM(lua_State* L, StkId func)
{
    const TValue* tm = luaT_gettmbyobj(L, func, TM_CALL);
    if (!ttisfunction(tm))
        luaG_typeerror(L, func, "call");
    for (StkId p = L->top; p > func; p--) // open space for metamethod
        setobj2s(L, p, p - 1);
    L->top++;              // stack space pre-allocated by the caller
    setobj2s(L, func, tm); // tag method is the new function to be called
}
