// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Canonical allocation sizes (docs/DETERMINISM.md, 5 and "Supported platforms").
//
// The GC paces itself by g->totalbytes, the sum of the sizes the VM asks the allocator for, so which GC step runs
// when, which weak entries clear and when destructors run are a function of those sizes; and the host sees every one
// of them through lua_Alloc. Every size the VM requests or charges is therefore the canonical size: the size the
// struct has on the 64-bit targets. On a 64-bit target each canonical size is sizeof. On wasm32 the struct is
// smaller (its pointers are 4 bytes) and the VM requests and charges the canonical size anyway, so the sequence of
// allocation sizes, totalbytes after each one and every GC step point are the same as on 64-bit; the cost is padding
// inside each block. moo_layout.cpp asserts each canonical size against the target: equal on 64-bit, at least the
// struct's size and alignment on wasm32. A type that is not listed here does not allocate: moo_sizeof of it does not
// compile.
#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(__wasm32__)
#define MOO_TARGET_WASM32 1
#else
#define MOO_TARGET_WASM32 0
#endif

struct lua_TValue;
struct LuaNode;
struct LuaTable;
struct TString;
struct Udata;
struct LuauBuffer;
struct LuauVector;
struct Proto;
struct LocVar;
struct UpVal;
struct Closure;
struct LuauClass;
struct LuauObject;
struct FeedbackVectorSlot;
struct CallInfo;
struct lua_State;
struct global_State;
struct MooVec3;
struct MooQuat;
struct MooTransform;

// the canonical size of T; a type without a specialization does not allocate
template<class T>
struct moo_canonical;

// a pointer is 8 bytes on every target
template<class T>
struct moo_canonical<T*>
{
    static constexpr size_t size = 8;
};

#define MOO_CANONICAL(T, n) \
    template<> \
    struct moo_canonical<T> \
    { \
        static constexpr size_t size = n; \
    }

// the same size on every target
MOO_CANONICAL(uint8_t, 1);
MOO_CANONICAL(int32_t, 4);
MOO_CANONICAL(uint32_t, 4); // Instruction
MOO_CANONICAL(lua_TValue, 16);
MOO_CANONICAL(LuaNode, 32);
MOO_CANONICAL(LuauVector, 24);
MOO_CANONICAL(FeedbackVectorSlot, 16);
MOO_CANONICAL(MooVec3, 40);
MOO_CANONICAL(MooQuat, 48);
MOO_CANONICAL(MooTransform, 72);

// smaller on wasm32
MOO_CANONICAL(LuaTable, 56);
MOO_CANONICAL(Proto, 224);
MOO_CANONICAL(LocVar, 24);
MOO_CANONICAL(UpVal, 48);
MOO_CANONICAL(LuauClass, 96);
MOO_CANONICAL(LuauObject, 48);
MOO_CANONICAL(CallInfo, 48);
MOO_CANONICAL(lua_State, 144);
MOO_CANONICAL(global_State, 19264);

#undef MOO_CANONICAL

#define moo_sizeof(T) (moo_canonical<T>::size)

// the canonical offsets of the payloads that follow a header, and of the two closure shapes
#define MOO_OFFSET_TSTRING_DATA 32
#define MOO_OFFSET_UDATA_DATA 32
#define MOO_OFFSET_BUFFER_DATA 16
#define MOO_OFFSET_CCLOSURE_UPVALS 56
#define MOO_OFFSET_LCLOSURE_UPREFS 40

// the main thread and the global state are one allocation (lstate.cpp LG)
#define MOO_SIZEOF_LG (moo_sizeof(lua_State) + moo_sizeof(global_State))

// the data offset of a lua_Page (lmem.cpp): the blocks per page follow from it
#define MOO_OFFSET_PAGE_DATA 64

// equal on a 64-bit target; at least the struct on wasm32, where the block has room to spare
#define MOO_LAYOUT_PIN(actual, canonical, message) \
    static_assert(MOO_TARGET_WASM32 ? size_t(actual) <= size_t(canonical) : size_t(actual) == size_t(canonical), message)
