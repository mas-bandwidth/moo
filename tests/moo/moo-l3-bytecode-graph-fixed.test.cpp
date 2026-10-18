// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// The bytecode-graph constant propagation (Sccp) folds numbers with moo_number.h, exactly as the compiler does, so a
// folded constant is the VM's result (docs/layer3/moo-l3-bytecode-graph-fixed.md; docs/DESIGN.md "Layer 3 as built").
#include "Luau/Sccp.h"
#include "Luau/BytecodeGraph.h"

#include "MooHost.h"

#include "doctest.h"

#include <vector>

using namespace Luau;
using namespace Luau::Bytecode;

TEST_SUITE_BEGIN("moo-l3-bytecode-graph-fixed");

namespace
{

uint64_t nextRandom(uint64_t& state)
{
    state += 0x9e3779b97f4a7c15ull;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

// 64 seeded raw Q48.16 operands, including the saturation edges (docs/DESIGN.md "Overflow")
std::vector<int64_t> rawOperands()
{
    std::vector<int64_t> v;
    v.push_back(0);
    v.push_back(1);
    v.push_back(-1);
    v.push_back(MOO_NUMBER_ONE);
    v.push_back(-MOO_NUMBER_ONE);
    v.push_back(MOO_NUMBER_MAX);
    v.push_back(-MOO_NUMBER_MAX);
    v.push_back(MOO_NUMBER_MAX - 1);
    v.push_back(-MOO_NUMBER_MAX + 1);
    v.push_back(int64_t(1) << 47);
    v.push_back(-(int64_t(1) << 47));
    uint64_t state = 1000;
    while (v.size() < 64)
    {
        uint64_t bits = nextRandom(state);
        int shift = int(nextRandom(state) % 63) + 1;
        uint64_t mag = (bits & ((1ull << shift) - 1)) | (1ull << shift);
        int64_t x = mag > uint64_t(INT64_MAX) ? INT64_MAX : int64_t(mag);
        v.push_back(((nextRandom(state) & 1) ? -x : x));
    }
    return v;
}

} // namespace

TEST_CASE("MooSccpFoldOperatorsMatchMooNumber")
{
    std::vector<int64_t> vals = rawOperands();
    for (int64_t a : vals)
    {
        for (int64_t b : vals)
        {
            CHECK(Luau::Bytecode::evaluateNumberBinaryOp(a, b, LuauOpcode::LOP_ADD) == moo_numadd(a, b));
            CHECK(Luau::Bytecode::evaluateNumberBinaryOp(a, b, LuauOpcode::LOP_SUB) == moo_numsub(a, b));
            CHECK(Luau::Bytecode::evaluateNumberBinaryOp(a, b, LuauOpcode::LOP_MUL) == moo_nummul(a, b));
            if (b != 0)
            {
                CHECK(Luau::Bytecode::evaluateNumberBinaryOp(a, b, LuauOpcode::LOP_DIV) == moo_numdiv(a, b));
                CHECK(Luau::Bytecode::evaluateNumberBinaryOp(a, b, LuauOpcode::LOP_IDIV) == moo_numidiv(a, b));
                CHECK(Luau::Bytecode::evaluateNumberBinaryOp(a, b, LuauOpcode::LOP_MOD) == moo_nummod(a, b));
            }
            else
            {
                CHECK(!Luau::Bytecode::evaluateNumberBinaryOp(a, b, LuauOpcode::LOP_DIV));
                CHECK(!Luau::Bytecode::evaluateNumberBinaryOp(a, b, LuauOpcode::LOP_IDIV));
                CHECK(!Luau::Bytecode::evaluateNumberBinaryOp(a, b, LuauOpcode::LOP_MOD));
            }
        }
    }
}

TEST_CASE("MooSccpPowNeverFolded")
{
    std::vector<int64_t> vals = rawOperands();
    for (int64_t a : vals)
        for (int64_t b : vals)
            CHECK(!Luau::Bytecode::evaluateNumberBinaryOp(a, b, LuauOpcode::LOP_POW));
}

TEST_CASE("MooSccpGraphFoldsAddOnRaw")
{
    BcFunction<BcVmConst> func;
    BcOp entry = func.entryBlock = func.addBlock();
    BcOp exit = func.exitBlock = func.addBlock();

    BcOp lhs = func.addInst(LOP_LOADN, entry, {func.addImmInt(3)}, Reg(0));
    BcOp rhs = func.addInst(LOP_LOADN, entry, {func.addImmInt(5)}, Reg(1));
    BcOp sum = func.addInst(LOP_ADD, entry, {lhs, rhs}, Reg(2));
    func.addInst(LOP_RETURN, entry, {func.addImmInt(1), sum});
    func.addEdge(entry, exit, BcBlockEdgeKind::Fallthrough);

    BcVmConstImpl impl(func);
    Sccp<BcVmConst> sccp(func, &impl);
    sccp.propagate();

    auto it = sccp.state.opConstness.find(sum);
    REQUIRE(it != nullptr);
    CHECK(it->kind == Constness::Constant);
    CHECK(impl.asNumber(it->constant.value()) == moo_numadd(moo_numfromint(3), moo_numfromint(5)));
}

TEST_SUITE_END();
