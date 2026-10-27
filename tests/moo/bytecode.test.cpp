// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Bytecode: the moo bytecode version, the raw Q48.16 number constants, and the refusals of Luau
// bytecode, an integer constant, an out-of-range number and a vector constant (docs/DESIGN.md
// "Layer 3 as built", "Literals").

#include "lua.h"
#include "lualib.h"
#include "Luau/Bytecode.h"
#include "Luau/BytecodeBuilder.h"
#include "Luau/BytecodeGraph.h"
#include "Luau/BytecodeDump.h"
#include "doctest.h"
#include "MooHost.h"

#include <limits>
#include <string>
#include <vector>

TEST_SUITE_BEGIN("bytecode");

// The moo bytecode version (docs/history/layer3/moo-l3-bytecode-version.md; docs/DESIGN.md "Literals" and "integer";
// docs/DETERMINISM.md 19): a moo blob starts with LBC_MOO_VERSION_FLAG | the moo version, its number constants are the
// raw Q48.16 value, and the VM refuses Luau bytecode, unknown moo versions, integer constants and out-of-domain numbers,
// each by name.
namespace bytecode_version
{

namespace
{

void putWord(std::string& s, uint32_t w)
{
    for (int i = 0; i < 4; ++i)
        s += char((w >> (8 * i)) & 0xff);
}

void putRaw(std::string& s, int64_t v)
{
    for (int i = 0; i < 8; ++i)
        s += char((uint64_t(v) >> (8 * i)) & 0xff);
}

// The body of a hand-built chunk in the format of Luau version 9 (LBC_VERSION_TARGET at BASE), types version 3: no
// strings, no userdata types, one proto `LOADK R0 K0; RETURN R0 1` with the one constant `constant` (its tag and bytes).
std::string luauV9Body(const std::string& constant)
{
    std::string s;
    s += char(9); // the Luau version
    s += char(3); // types version
    s += char(0); // string count
    s += char(0); // userdata type remapping: end
    s += char(1); // proto count

    s += char(1); // maxstacksize
    s += char(0); // numparams
    s += char(0); // nups
    s += char(0); // is_vararg
    s += char(0); // flags
    s += char(0); // type info size

    s += char(2);                                     // sizecode
    putWord(s, LOP_LOADK | (0 << 8) | (0 << 16));     // LOADK R0 K0
    putWord(s, LOP_RETURN | (0 << 8) | (2 << 16));    // RETURN R0 1 (B = results + 1)

    s += char(1); // sizek
    s += constant;

    s += char(0); // sizep
    s += char(0); // linedefined
    s += char(0); // debugname
    s += char(0); // lineinfo
    s += char(0); // debuginfo

    s += char(0); // main proto
    return s;
}

std::string numberConstant(int64_t raw)
{
    std::string c(1, char(LBC_CONSTANT_NUMBER));
    putRaw(c, raw);
    return c;
}

std::string mooBlob(const std::string& luauBody)
{
    return std::string(1, char(LBC_MOO_VERSION_FLAG | LBC_MOO_VERSION_TARGET)) + luauBody;
}

// luau_load and run: "" and the raw result on success, else the load or run error
std::string loadAndRun(lua_State* L, const std::string& blob, int64_t* result = nullptr)
{
    if (luau_load(L, "=moo", blob.data(), blob.size(), 0) != 0)
    {
        std::string err = lua_tostring(L, -1);
        lua_pop(L, 1);
        return err;
    }
    if (lua_pcall(L, 0, 1, 0) != 0)
    {
        std::string err = std::string("run: ") + lua_tostring(L, -1);
        lua_pop(L, 1);
        return err;
    }
    if (result)
        *result = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return "";
}

// a moo blob built with BytecodeBuilder directly: one function returning the number constant with raw value k
std::string builtBlob(int64_t k, std::string* functionData = nullptr)
{
    Luau::BytecodeBuilder bcb;
    uint32_t fid = bcb.beginFunction(0);
    int32_t kid = bcb.addConstantNumberRaw(k);
    REQUIRE(kid == 0);
    bcb.emitAD(LOP_LOADK, 0, int16_t(kid));
    bcb.emitABC(LOP_RETURN, 0, 2, 0);
    bcb.endFunction(1, 0);
    bcb.setMainFunction(fid);
    bcb.finalize();
    if (functionData)
        *functionData = bcb.getFunctionData(fid);
    return bcb.getBytecode();
}

// splitmix64: a seeded, platform-independent sequence
uint64_t nextRandom(uint64_t& state)
{
    uint64_t z = (state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

// 4096 raw values across the whole domain [-INT64_MAX, INT64_MAX]: the edges, then every magnitude scale
std::vector<int64_t> seededRawValues()
{
    std::vector<int64_t> values = {
        INT64_MAX, -INT64_MAX, 1, -1, 0, int64_t(1) << 37, (int64_t(1) << 37) + 1, -(int64_t(1) << 37), MOO_NUMBER_ONE, -MOO_NUMBER_ONE,
        6554, // 0.1
        INT64_MAX - 1, -INT64_MAX + 1, int64_t(1) << 53, (int64_t(1) << 53) + 1, int64_t(1) << 62,
    };

    uint64_t state = 1000;
    while (values.size() < 4096)
    {
        uint64_t bits = nextRandom(state);
        int shift = int(nextRandom(state) % 64);
        int64_t v = int64_t(bits >> shift);
        if (v == INT64_MIN)
            continue;
        // every magnitude scale in both halves of the domain
        values.push_back((nextRandom(state) & 1) ? -v : v);
    }
    return values;
}

} // namespace

// DESIGN.md "Literals": the VM refuses Luau bytecode by version, by name. The hand-built body loads as moo bytecode, so
// the refusal is the version byte alone; every 7-bit version (each a Luau version, now or after a merge) is refused.
TEST_CASE("BytecodeRefusesLuauByName")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);

    std::string body = luauV9Body(numberConstant(98304)); // 1.5

    int64_t result = 0;
    CHECK(loadAndRun(L, mooBlob(body), &result) == "");
    CHECK(result == 98304);

    for (int version = 1; version <= 127; ++version)
    {
        std::string blob = body;
        blob[0] = char(version);
        std::string expected = "moo: this is Luau bytecode (version " + std::to_string(version) + "), not moo bytecode";
        CHECK_MESSAGE(loadAndRun(L, blob) == expected, "version " << version);
    }

    // the Luau versions at BASE by name: the target, the maximum and the classes version
    for (int version : {int(LBC_VERSION_TARGET), int(LBC_VERSION_MAX), int(LBC_VERSION_CLASSES)})
    {
        std::string blob = body;
        blob[0] = char(version);
        CHECK(loadAndRun(L, blob).find("this is Luau bytecode") != std::string::npos);
    }

    // the error blob (version 0) still carries the compiler's message
    std::string error = std::string(1, char(0)) + ":1: some compile error";
    CHECK(loadAndRun(L, error) == "moo:1: some compile error");

    lua_close(L);
}

// Bytecode.h "moo bytecode versions": a moo version outside [LBC_MOO_VERSION_MIN, LBC_MOO_VERSION_MAX] is refused by name.
TEST_CASE("BytecodeRefusesUnknownMooVersion")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);

    std::string blob = mooBlob(luauV9Body(numberConstant(98304)));
    for (int mooversion = 0; mooversion <= 127; ++mooversion)
    {
        if (mooversion >= LBC_MOO_VERSION_MIN && mooversion <= LBC_MOO_VERSION_MAX)
            continue;
        blob[0] = char(LBC_MOO_VERSION_FLAG | mooversion);
        std::string expected = "moo: moo bytecode version mismatch (expected [1..1], got " + std::to_string(mooversion) + ")";
        CHECK_MESSAGE(loadAndRun(L, blob) == expected, "moo version " << mooversion);
    }

    // a moo blob whose Luau format version is unknown keeps Luau's refusal
    blob = mooBlob(luauV9Body(numberConstant(98304)));
    blob[1] = char(2);
    CHECK(loadAndRun(L, blob).find("bytecode version mismatch (expected [3..14], got 2)") != std::string::npos);

    lua_close(L);
}

// DESIGN.md "integer": moo has one number type, so a hand-built chunk with an integer constant is refused by name.
TEST_CASE("BytecodeRefusesIntegerConstant")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);

    std::string constant;
    constant += char(LBC_CONSTANT_INTEGER);
    constant += char(0); // not negative
    constant += char(5); // magnitude, varint

    CHECK(loadAndRun(L, mooBlob(luauV9Body(constant))) == "moo: moo bytecode has no integer constant");

    lua_close(L);
}

// DESIGN.md "Layer 3 as built": the domain is [-INT64_MAX, INT64_MAX] raw, so INT64_MIN is refused by name.
TEST_CASE("BytecodeRefusesNumberOutsideTheDomain")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);

    CHECK(loadAndRun(L, mooBlob(luauV9Body(numberConstant(INT64_MIN)))) == "moo: moo bytecode number constant out of range");

    int64_t result = 0;
    CHECK(loadAndRun(L, mooBlob(luauV9Body(numberConstant(-INT64_MAX))), &result) == "");
    CHECK(result == -INT64_MAX);

    lua_close(L);
}

// DESIGN.md "Literals": every raw value survives addConstantNumberRaw -> serialize -> luau_load bit exact, carried as
// 8 bytes little-endian after the LBC_CONSTANT_NUMBER tag.
TEST_CASE("BytecodeNumberConstantsAreRaw")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);

    std::vector<int64_t> values = seededRawValues();
    REQUIRE(values.size() == 4096);

    int mismatches = 0;
    for (int64_t k : values)
    {
        std::string blob = builtBlob(k);

        REQUIRE(uint8_t(blob[0]) == (LBC_MOO_VERSION_FLAG | LBC_MOO_VERSION_TARGET));
        REQUIRE(uint8_t(blob[1]) == Luau::BytecodeBuilder::getVersion());

        std::string expectedBytes = numberConstant(k);
        CHECK(blob.find(expectedBytes) != std::string::npos);

        int64_t result = 0;
        std::string err = loadAndRun(L, blob, &result);
        if (err != "" || result != k)
        {
            ++mismatches;
            CHECK_MESSAGE(false, "raw " << k << ": " << err << " got " << result);
        }
    }
    CHECK(mismatches == 0);

    lua_close(L);
}

// The moo-bytecode tool's reader and writer (BytecodeGraph.cpp) carry the raw value, so a round trip is bit exact.
TEST_CASE("BytecodeGraphRoundTripIsRaw")
{
    for (int64_t k : seededRawValues())
    {
        std::string functionData;
        std::string blob = builtBlob(k, &functionData);

        std::vector<std::string_view> strings;
        std::optional<Luau::Bytecode::CompTimeBcFunction> fn = Luau::Bytecode::fromFunctionBytecode(functionData, strings);
        REQUIRE(fn);
        REQUIRE(fn->constants.size() == 1);
        REQUIRE(fn->constants[0].kind == Luau::Bytecode::BcVmConstKind::Number);
        CHECK(fn->constants[0].valueNumber == k);

        Luau::BytecodeBuilder rebuilt;
        Luau::Bytecode::toFunctionBytecode(rebuilt, *fn);
        CHECK(rebuilt.getFunctionData(0) == functionData);
    }
}

// DESIGN.md "Boundary": addConstantNumber(double), kept for the compiler's callers, converts once by the boundary rule,
// ties to even, saturating, NaN to 0.
TEST_CASE("BytecodeDoubleConstantsConvertByTheBoundaryRule")
{
    using Luau::BytecodeBuilder;
    CHECK(BytecodeBuilder::mooNumberFromDouble(1.5) == 98304);
    CHECK(BytecodeBuilder::mooNumberFromDouble(-1.0) == -65536);
    CHECK(BytecodeBuilder::mooNumberFromDouble(0.1) == 6554);
    CHECK(BytecodeBuilder::mooNumberFromDouble(-0.0) == 0);
    CHECK(BytecodeBuilder::mooNumberFromDouble(137438953472.0) == (int64_t(1) << 53));
    CHECK(BytecodeBuilder::mooNumberFromDouble(1.0 / 131072.0) == 0);        // half a unit, ties to even
    CHECK(BytecodeBuilder::mooNumberFromDouble(3.0 / 131072.0) == 2);        // one and a half units, ties to even
    CHECK(BytecodeBuilder::mooNumberFromDouble(1e300) == INT64_MAX);
    CHECK(BytecodeBuilder::mooNumberFromDouble(-1e300) == -INT64_MAX);
    CHECK(BytecodeBuilder::mooNumberFromDouble(std::numeric_limits<double>::infinity()) == INT64_MAX);
    CHECK(BytecodeBuilder::mooNumberFromDouble(-std::numeric_limits<double>::infinity()) == -INT64_MAX);
    CHECK(BytecodeBuilder::mooNumberFromDouble(std::numeric_limits<double>::quiet_NaN()) == 0);

    // exactly the old load-time bridge (moo_numfromdoublevalue), so the compiler's output values are unchanged: seeded
    // IEEE bit patterns of both signs, every exponent from the subnormals past 2^47, and the ties and edges by name
    std::vector<double> doubles = {0.0, -0.0, 140737488355328.0, -140737488355328.0, 140737488355327.99998474121,
        -140737488355327.99998474121, 1.0 / 131072.0, -1.0 / 131072.0, 3.0 / 131072.0, -3.0 / 131072.0, 5e-324, -5e-324};
    uint64_t state = 1000;
    while (doubles.size() < 8192)
    {
        uint64_t mantissa = nextRandom(state) & ((uint64_t(1) << 52) - 1);
        uint64_t exponent = 1023 - 40 + nextRandom(state) % 100; // 2^-40 .. 2^59
        if (nextRandom(state) % 8 == 0)
            mantissa &= ~((uint64_t(1) << 36) - 1); // short mantissas: exact values and ties
        uint64_t bits = (uint64_t(nextRandom(state) & 1) << 63) | (exponent << 52) | mantissa;
        double d;
        memcpy(&d, &bits, sizeof(d));
        doubles.push_back(d);
    }
    for (double d : doubles)
        CHECK_MESSAGE(BytecodeBuilder::mooNumberFromDouble(d) == moo_numfromdoublevalue(d), "double " << d);
}

// Bytecode.h "moo bytecode versions": a blob that ends after the moo version byte is refused by name, not read past.
TEST_CASE("BytecodeRefusesTruncatedHeader")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);

    std::string blob(1, char(LBC_MOO_VERSION_FLAG | LBC_MOO_VERSION_TARGET));
    CHECK(loadAndRun(L, blob) == "moo: moo bytecode is truncated");

    lua_close(L);
}

// A refusal in a later proto, after an earlier proto finished loading, leaves the VM consistent through a full GC.
TEST_CASE("BytecodeRefusalMidChunkIsGcSafe")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    // two protos: proto 0 is valid, proto 1 carries an integer constant
    std::string body = luauV9Body(numberConstant(98304));
    std::string proto = body.substr(5, body.size() - 6); // the one proto, without the header and the main id
    std::string intConstant;
    intConstant += char(LBC_CONSTANT_INTEGER);
    intConstant += char(0);
    intConstant += char(5);
    std::string badBody = luauV9Body(intConstant);
    std::string badProto = badBody.substr(5, badBody.size() - 6);

    std::string blob = mooBlob(body.substr(0, 4) + char(2) + proto + badProto + char(0));
    CHECK(loadAndRun(L, blob) == "moo: moo bytecode has no integer constant");

    lua_gc(L, LUA_GCCOLLECT, 0);
    CHECK(mooRun(L, "return 1 + 1") == "2");

    lua_close(L);
}

// docs/DESIGN.md "Number formatting": the dump prints a fractional constant as the shortest decimal that reads back.
TEST_CASE("BytecodeDumpPrintsShortest")
{
    Luau::BytecodeBuilder bcb;
    bcb.setDumpFlags(Luau::BytecodeBuilder::Dump_Code);
    uint32_t fid = bcb.beginFunction(0);
    bcb.emitAD(LOP_LOADK, 0, int16_t(bcb.addConstantNumberRaw(6554)));
    bcb.emitAD(LOP_LOADK, 1, int16_t(bcb.addConstantNumberRaw(-INT64_MAX)));
    bcb.emitABC(LOP_RETURN, 0, 3, 0);
    bcb.endFunction(2, 0);
    bcb.setMainFunction(fid);
    bcb.finalize();
    std::string dump = bcb.dumpFunction(fid);
    CHECK_MESSAGE(dump.find("LOADK R0 K0 [0.1]") != std::string::npos, dump);
    CHECK_MESSAGE(dump.find("LOADK R1 K1 [-140737488355327.99998]") != std::string::npos, dump);

    // the graph tool's dump prints the same way
    std::vector<std::string_view> strings;
    std::optional<Luau::Bytecode::CompTimeBcFunction> fn = Luau::Bytecode::fromFunctionBytecode(bcb.getFunctionData(fid), strings);
    REQUIRE(fn);
    std::string graphDump = Luau::Bytecode::toString(*fn, true);
    CHECK_MESSAGE(graphDump.find("(0.1)") != std::string::npos, graphDump);
}

// The graph reader refuses INT64_MIN, outside the domain, as the VM does.
TEST_CASE("BytecodeGraphRefusesNumberOutsideTheDomain")
{
    std::string data;
    builtBlob(1, &data);
    size_t at = data.find(numberConstant(1));
    REQUIRE(at != std::string::npos);
    std::string bad = data.substr(0, at) + numberConstant(INT64_MIN) + data.substr(at + 9);

    std::vector<std::string_view> strings;
    CHECK(!Luau::Bytecode::fromFunctionBytecode(bad, strings));
}

} // namespace bytecode_version

// The moo bytecode has no vector constant (docs/history/layer3/moo-l3-vector-constant-refused.md; docs/DESIGN.md
// "vec3, quat and transform"): the builder never writes LBC_CONSTANT_VECTOR or LBC_CONSTANT_VECTORD, and the VM
// refuses a hand-built chunk holding either tag by name.
namespace vector_constant
{

namespace
{

// The body of a hand-built chunk in the format of Luau version 9 (LBC_VERSION_TARGET at BASE), types version 3: no
// strings, no userdata types, one proto with no instructions, holding the one constant `constant` (its tag and bytes).
// The constant is read before any instruction runs, so the refusal fires on load.
std::string chunkBody(const std::string& constant)
{
    std::string s;
    s += char(9); // the Luau version
    s += char(3); // types version
    s += char(0); // string count
    s += char(0); // userdata type remapping: end
    s += char(1); // proto count

    s += char(1); // maxstacksize
    s += char(0); // numparams
    s += char(0); // nups
    s += char(0); // is_vararg
    s += char(0); // flags
    s += char(0); // type info size

    s += char(0); // sizecode: no instructions, the constant is still read
    s += char(1); // sizek
    s += constant;

    s += char(0); // sizep
    s += char(0); // linedefined
    s += char(0); // debugname
    s += char(0); // lineinfo
    s += char(0); // debuginfo

    s += char(0); // main proto
    return s;
}

std::string mooBlob(const std::string& body)
{
    return std::string(1, char(LBC_MOO_VERSION_FLAG | LBC_MOO_VERSION_TARGET)) + body;
}

// luau_load: "" on success, else the load error
std::string loadError(lua_State* L, const std::string& blob)
{
    if (luau_load(L, "=moo", blob.data(), blob.size(), 0) != 0)
    {
        std::string err = lua_tostring(L, -1);
        lua_pop(L, 1);
        return err;
    }
    lua_pop(L, 1);
    return "";
}

} // namespace

// DESIGN.md "vec3, quat and transform": the VM refuses a chunk holding a float vector constant by name.
TEST_CASE("BytecodeRefusesVectorConstantF")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);

    std::string constant(1, char(LBC_CONSTANT_VECTOR));
    constant.append(16, '\0'); // four float32 payload bytes; the refusal fires before they are read

    CHECK(loadError(L, mooBlob(chunkBody(constant))) == "moo: moo bytecode has no vector constant");

    lua_close(L);
}

// DESIGN.md "vec3, quat and transform": the VM refuses a chunk holding a double vector constant by name.
TEST_CASE("BytecodeRefusesVectorConstantD")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);

    std::string constant(1, char(LBC_CONSTANT_VECTORD));
    constant.append(32, '\0'); // four float64 payload bytes; the refusal fires before they are read

    CHECK(loadError(L, mooBlob(chunkBody(constant))) == "moo: moo bytecode has no vector constant");

    lua_close(L);
}

// The same hand-built chunk with an ordinary number constant still loads: the refusal is for the vector tags alone.
TEST_CASE("BytecodeVectorRefusalIsSpecific")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);

    std::string constant(1, char(LBC_CONSTANT_NUMBER));
    constant.append(8, '\0'); // raw Q48.16 value 0

    CHECK(loadError(L, mooBlob(chunkBody(constant))) == "");

    lua_close(L);
}

} // namespace vector_constant

TEST_SUITE_END();
