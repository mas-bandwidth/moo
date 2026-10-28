// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// The compiler is deterministic (docs/DETERMINISM.md, 25): the same source and options give the same bytecode,
// byte for byte, whatever the addresses the compiler's objects land at, on every platform and compiler.
//
// - CompilerOutputIgnoresAddresses: every corpus file at -O0..2 x -g0..2, compiled ten times in one process with
//   random-sized blocks allocated and freed between compiles, so the AST and the compiler's tables move.
// - CompilerBytecodePins: every corpus file at -O0..2 x -g0..2 hashes to tests/determinism/bytecode.txt, the pins
//   every CI job reproduces, or bytecode-checked.txt in a checked build, whose compiler leaves a saturating fold to
//   the VM (docs/DESIGN.md "Overflow"). MOO_BYTECODE_PINS_WRITE=<file> writes this platform's hashes (re-pinning, and the
//   nightly cross-OS comparison).
// - CompilerGeneratedProgramsIgnoreAddresses: seeded generated programs, each compiled in several allocation states.
//   MOO_COMPILE_FUZZ_N sets the count (the nightly raises it); MOO_COMPILE_FUZZ_HASHES=<file> writes their hashes.
//
// The corpus is every script in tests/conformance, tests/conformance/moo, tests/conformance/moo/spec,
// tests/determinism and tests/determinism/compile. Compiling is all these tests do: no time, no sockets.

#include "Luau/BytecodeBuilder.h"
#include "Luau/Common.h"
#include "Luau/Compiler.h"
#include "Luau/ExperimentalFlags.h"
#include "Luau/ParseResult.h"
#include "ScopedFlags.h"
#include "moo_number.h"
#include "doctest.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

LUAU_FASTFLAG(DebugLuauUserDefinedClasses)

TEST_SUITE_BEGIN("compile_determinism");

namespace
{

const char* const kCorpusDirs[] = {"conformance", "conformance/moo", "conformance/moo/spec", "determinism", "determinism/compile"};

std::string testsDir()
{
    return std::string(LUAU_CONFORMANCE_SOURCE_DIR) + "/..";
}

struct CorpusFile
{
    std::string name; // relative to tests/, with forward slashes
    std::string source;
};

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

const std::vector<CorpusFile>& corpus()
{
    static const std::vector<CorpusFile> files = []
    {
        std::vector<CorpusFile> result;
        for (const char* dir : kCorpusDirs)
        {
            std::filesystem::path path = std::filesystem::path(testsDir()) / dir;
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(path))
            {
                if (!entry.is_regular_file() || entry.path().extension() != ".luau")
                    continue;
                result.push_back({std::string(dir) + "/" + entry.path().filename().string(), readFile(entry.path())});
            }
        }
        std::sort(
            result.begin(),
            result.end(),
            [](const CorpusFile& a, const CorpusFile& b)
            {
                return a.name < b.name;
            }
        );
        return result;
    }();
    return files;
}

// moo-compile's flags for the life of a test: every Luau flag on but the experimental analysis ones, as
// CLI/src/Flags.cpp setLuauFlagsDefault sets them, so the pins hold for both. The previous values come back after.
struct CliFlags
{
    std::vector<std::pair<Luau::FValue<bool>*, bool>> saved;

    CliFlags()
    {
        for (Luau::FValue<bool>* flag = Luau::FValue<bool>::list; flag; flag = flag->next)
        {
            saved.emplace_back(flag, flag->value);
            if (strncmp(flag->name, "Luau", 4) == 0 && !Luau::isAnalysisFlagExperimental(flag->name))
                flag->value = true;
        }
    }

    ~CliFlags()
    {
        for (auto& [flag, value] : saved)
            flag->value = value;
    }

    CliFlags(const CliFlags&) = delete;
    CliFlags& operator=(const CliFlags&) = delete;
};

// One compile's result: the bytecode, or the error text moo-compile prints without its file name prefix, one line per
// error: "(<line>,<column>): SyntaxError: <message>" or "(<line>,<column>): CompileError: <message>"
// (CLI/src/Compile.cpp report).
struct Compiled
{
    bool ok = false;
    std::string data;

    bool operator==(const Compiled& other) const
    {
        return ok == other.ok && data == other.data;
    }

    bool operator!=(const Compiled& other) const
    {
        return !(*this == other);
    }
};

std::string errorLine(const Luau::Location& location, const char* type, const char* message)
{
    return "(" + std::to_string(location.begin.line + 1) + "," + std::to_string(location.begin.column + 1) + "): " + type + ": " +
           message + "\n";
}

// moo-compile --binary -O<o> -g<g>: the same options (CLI/src/Compile.cpp copts) under CliFlags
Compiled compileBytes(const std::string& source, int o, int g)
{
    Luau::CompileOptions options = {};
    options.optimizationLevel = o;
    options.debugLevel = g;
    options.typeInfoLevel = 0;

    Compiled result;
    try
    {
        Luau::BytecodeBuilder bcb;
        Luau::compileOrThrow(bcb, source, options);
        result.ok = true;
        result.data = bcb.getBytecode();
    }
    catch (Luau::ParseErrors& e)
    {
        for (const Luau::ParseError& error : e.getErrors())
            result.data += errorLine(error.getLocation(), "SyntaxError", error.what());
    }
    catch (Luau::CompileError& e)
    {
        result.data = errorLine(e.getLocation(), "CompileError", e.what());
    }
    return result;
}

uint64_t fnv1a(const std::string& bytes)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char c : bytes)
        h = (h ^ c) * 0x100000001b3ull;
    return h;
}

std::string hex64(const std::string& bytes)
{
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)fnv1a(bytes));
    return buf;
}

// the pin of one compile: the FNV-1a 64 of the bytecode in hex, or error-<the FNV-1a 64 of the error text>, so a
// changed or reordered error message moves the pin too (moo-compile exits 1)
std::string pinOf(const Compiled& compiled)
{
    return compiled.ok ? hex64(compiled.data) : "error-" + hex64(compiled.data);
}

// Allocates and frees random-sized blocks, keeping some alive, so each compile's allocations land at new addresses.
struct HeapDisturber
{
    std::mt19937_64 rng;
    std::vector<std::unique_ptr<char[]>> live;

    explicit HeapDisturber(uint64_t seed)
        : rng(seed)
    {
    }

    void step()
    {
        size_t frees = live.empty() ? 0 : size_t(rng() % (live.size() + 1));
        for (size_t i = 0; i < frees && !live.empty(); ++i)
        {
            size_t at = size_t(rng() % live.size());
            live[at] = std::move(live.back());
            live.pop_back();
        }

        size_t allocs = 1 + size_t(rng() % 48);
        for (size_t i = 0; i < allocs && live.size() < 512; ++i)
        {
            size_t size = 1 + size_t(rng() % 65536);
            std::unique_ptr<char[]> block(new char[size]);
            block[0] = char(size); // touched, so the allocation is real
            live.push_back(std::move(block));
        }
    }
};

// Compiles `source` `times` times, disturbing the heap before every compile after the first, and returns the number of
// compiles whose output differs from the first.
int countDivergent(HeapDisturber& heap, const std::string& source, int o, int g, int times)
{
    Compiled first = compileBytes(source, o, g);
    int divergent = 0;
    for (int i = 1; i < times; ++i)
    {
        heap.step();
        if (compileBytes(source, o, g) != first)
            ++divergent;
    }
    return divergent;
}

// A seeded generator of syntactically valid programs. They are compiled, never run, so they need not type check.
// Every random draw is its own statement: C++ leaves the order of the operands of + and of a call's arguments
// unspecified, so "expr() + expr()" draws in a different order under gcc, clang and MSVC, and the programs, not the
// compiler, would differ by platform.
struct ProgramGen
{
    std::mt19937_64 rng;
    std::string out;
    int depth = 0;
    int loops = 0;
    int names = 0;

    explicit ProgramGen(uint64_t seed)
        : rng(seed)
    {
    }

    int pick(int n)
    {
        return int(rng() % uint64_t(n));
    }

    std::string number()
    {
        switch (pick(5))
        {
        case 0:
            return std::to_string(pick(20));
        case 1:
        {
            std::string whole = std::to_string(pick(100000));
            return whole + "." + std::to_string(pick(1000));
        }
        case 2:
            return "0x" + std::to_string(pick(9000) + 1000);
        case 3:
        {
            std::string mantissa = std::to_string(pick(10));
            return mantissa + "e" + std::to_string(pick(5));
        }
        default:
            return std::to_string(pick(16) + 1);
        }
    }

    std::string var()
    {
        static const char* const pool[] = {"a", "b", "c", "x", "y", "t", "s", "n", "math", "string", "table"};
        return pool[pick(int(sizeof(pool) / sizeof(pool[0])))];
    }

    std::string expr()
    {
        if (depth > 4)
        {
            if (pick(2))
                return number();
            return var();
        }
        ++depth;
        std::string e;
        static const char* const binops[] = {"+", "-", "*", "/", "//", "%", "^", "..", "<", "<=", "==", "~=", "and", "or"};
        switch (pick(12))
        {
        case 0:
        case 1:
            e = number();
            break;
        case 2:
            e = var();
            break;
        case 3:
        {
            std::string l = expr();
            const char* op = binops[pick(int(sizeof(binops) / sizeof(binops[0])))];
            std::string r = expr();
            e = "(" + l + " " + op + " " + r + ")";
            break;
        }
        case 4:
            e = "\"s" + std::to_string(pick(50)) + "\"";
            break;
        case 5:
        {
            std::string a = expr();
            std::string b = expr();
            std::string k1 = std::to_string(pick(6));
            std::string v1 = expr();
            std::string k2 = std::to_string(pick(6));
            std::string v2 = expr();
            e = "{ " + a + ", " + b + ", k" + k1 + " = " + v1 + ", k" + k2 + " = " + v2 + " }";
            break;
        }
        case 6:
        {
            std::string object = var();
            std::string method = var();
            std::string arg = expr();
            e = object + "." + method + "(" + arg + ")";
            break;
        }
        case 7:
            e = "`v{(" + expr() + ")}w`"; // parenthesized: "{{" is not allowed in an interpolated string
            break;
        case 8:
            e = "function(p, q) return " + expr() + " end";
            break;
        case 9:
        {
            std::string c = expr();
            std::string t = expr();
            std::string f = expr();
            e = "if " + c + " then " + t + " else " + f;
            break;
        }
        case 10:
            e = "-" + number();
            break;
        default:
            e = "#" + var();
            break;
        }
        --depth;
        return e;
    }

    void block(int statements)
    {
        for (int i = 0; i < statements; ++i)
            stat();
    }

    void stat()
    {
        if (depth > 3)
        {
            std::string target = var();
            out += target + " = " + expr() + "\n";
            return;
        }
        ++depth;
        switch (pick(10))
        {
        case 0:
            out += "local l" + std::to_string(names++) + " = " + expr() + "\n";
            break;
        case 1:
        {
            std::string target = var();
            out += target + " = " + expr() + "\n";
            break;
        }
        case 2:
            out += "if " + expr() + " then\n";
            block(1 + pick(3));
            out += "else\n";
            block(pick(2));
            out += "end\n";
            break;
        case 3:
        {
            std::string from = std::to_string(pick(3));
            out += "for i = " + from + ", " + number() + " do\n";
            ++loops;
            block(1 + pick(3));
            --loops;
            out += "end\n";
            break;
        }
        case 4:
            out += "while " + expr() + " do\n";
            ++loops;
            block(1 + pick(2));
            out += "break\n";
            --loops;
            out += "end\n";
            break;
        case 5:
        {
            int outerLoops = loops; // a function body is outside every loop, so it may not break
            loops = 0;
            out += "local function lf" + std::to_string(names++) + "(p, q)\n";
            block(1 + pick(3));
            out += "return " + expr() + "\nend\n";
            loops = outerLoops;
            break;
        }
        case 6:
        {
            std::string key = expr();
            out += "t[" + key + "] = " + expr() + "\n";
            break;
        }
        case 7:
        {
            std::string callee = var();
            std::string a = expr();
            std::string b = expr();
            out += callee + "(" + a + ", " + b + ")\n";
            break;
        }
        case 8:
        {
            std::string value = expr();
            out += "do\nlocal u = " + value + "\n" + var() + " = function() return u end\nend\n";
            break;
        }
        default:
            if (loops > 0)
                out += "if " + expr() + " then break end\n";
            else
            {
                std::string target = var();
                out += target + " ..= " + expr() + "\n";
            }
            break;
        }
        --depth;
    }

    std::string program()
    {
        out = "local a, b, c, x, y, t, s, n = 1, 2, 3, 4, 5, {}, \"\", 0\n";
        int exportsCount = pick(12);
        for (int i = 0; i < exportsCount; ++i)
        {
            if (pick(3) == 0)
                out += "export local ev" + std::to_string(i) + " = " + expr() + "\n";
            else
            {
                out += "export function ef" + std::to_string(i) + "(p, q)\n";
                block(1 + pick(3));
                out += "return " + expr() + "\nend\n";
            }
            loops = 0;
        }
        block(3 + pick(8));
        return out;
    }
};

int envInt(const char* name, int fallback)
{
    const char* value = getenv(name);
    return value && *value ? atoi(value) : fallback;
}

} // namespace

TEST_CASE("CompilerOutputIgnoresAddresses")
{
    CliFlags flags;
    HeapDisturber heap(0x4d4f4f434f4d50ULL); // "MOOCOMP"
    int divergent = 0;
    for (const CorpusFile& file : corpus())
        for (int o = 0; o <= 2; ++o)
            for (int g = 0; g <= 2; ++g)
            {
                int n = countDivergent(heap, file.source, o, g, 10);
                if (n != 0 && divergent++ < 10)
                    MESSAGE(file.name << " -O" << o << " -g" << g << ": " << n << " of 9 recompiles differ from the first");
            }
    CHECK_EQ(divergent, 0);
    CHECK(corpus().size() >= 100);
}

// The export table of classes is compiled only under the debug flag; it gets the same check on the class scripts.
TEST_CASE("CompilerClassExportsIgnoreAddresses")
{
    CliFlags flags;
    ScopedFastFlag classes{FFlag::DebugLuauUserDefinedClasses, true};
    std::string source = "export class A\n    public x\nend\nexport class B\n    public y\nend\nexport class C\nend\n"
                         "export class D\n    function f(self) return self end\nend\nexport class E\nend\nexport class F\nend\n"
                         "export function g() return A, B, C end\nexport function h() return D, E, F end\n";
    HeapDisturber heap(0x434c41535345ULL); // "CLASSE"
    REQUIRE(compileBytes(source, 1, 1).ok);
    for (int o = 0; o <= 2; ++o)
        for (int g = 0; g <= 2; ++g)
            CHECK_EQ(countDivergent(heap, source, o, g, 10), 0);
}

TEST_CASE("CompilerBytecodePins")
{
    CliFlags flags;
    std::map<std::string, std::string> actual;
    for (const CorpusFile& file : corpus())
        for (int o = 0; o <= 2; ++o)
            for (int g = 0; g <= 2; ++g)
                actual[file.name + " O" + std::to_string(o) + " g" + std::to_string(g)] = pinOf(compileBytes(file.source, o, g));

    if (const char* write = getenv("MOO_BYTECODE_PINS_WRITE"); write && *write)
    {
        std::ofstream out(write, std::ios::binary);
        out << "# The compiler's bytecode pins: tests/<file> O<-O> g<-g> FNV-1a 64 of moo-compile --binary, or\n";
        out << "# error-<FNV-1a 64 of its error lines without the file name>.\n";
        out << "# Every CI job reproduces them (tests/moo/compile_determinism.test.cpp, tools/moo-check-compile.py).\n";
        out << "# An intended compiler change re-pins in its own pull request: MOO_BYTECODE_PINS_WRITE=<this file> Moo.Test.\n";
        out << "# " << (MOO_OVERFLOW_ERRORS ? "A checked build (MOO_OVERFLOW_ERRORS=ON): saturating folds are left to the VM."
                                            : "A release build: saturating folds fold.") << "\n";
        for (const auto& [key, pin] : actual)
            out << key << " " << pin << "\n";
    }

    const char* pinFile = MOO_OVERFLOW_ERRORS ? "bytecode-checked.txt" : "bytecode.txt";
    std::ifstream in(testsDir() + "/determinism/" + pinFile, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "cannot read tests/determinism/" << pinFile);
    std::map<std::string, std::string> pinned;
    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty() || line[0] == '#')
            continue;
        size_t at = line.rfind(' ');
        REQUIRE(at != std::string::npos);
        pinned[line.substr(0, at)] = line.substr(at + 1);
    }

    int mismatches = 0;
    for (const auto& [key, pin] : actual)
    {
        auto it = pinned.find(key);
        std::string want = it == pinned.end() ? "(no pin)" : it->second;
        if (want != pin && mismatches++ < 20)
            MESSAGE(key << ": " << pin << ", pinned " << want);
    }
    for (const auto& [key, pin] : pinned)
        if (actual.find(key) == actual.end() && mismatches++ < 20)
            MESSAGE(key << ": pinned " << pin << ", but the file is not in the corpus");
    CHECK_EQ(mismatches, 0);
    CHECK_EQ(pinned.size(), actual.size());
}

TEST_CASE("CompilerGeneratedProgramsIgnoreAddresses")
{
    CliFlags flags;
    int count = envInt("MOO_COMPILE_FUZZ_N", 64);
    const char* hashesPath = getenv("MOO_COMPILE_FUZZ_HASHES");
    std::ofstream hashes;
    if (hashesPath && *hashesPath)
        hashes.open(hashesPath, std::ios::binary);

    HeapDisturber heap(0x46555a5a4d4f4fULL); // "FUZZMOO"
    int divergent = 0;
    int compiled = 0;
    std::string rejected; // the first program the compiler refuses, shown if too many are refused
    for (int i = 0; i < count; ++i)
    {
        std::string source = ProgramGen(uint64_t(i) * 0x9e3779b97f4a7c15ULL + 1).program();
        for (int og = 0; og <= 2; ++og)
        {
            Compiled first = compileBytes(source, og, og);
            compiled += first.ok ? 1 : 0;
            if (!first.ok && rejected.empty())
                rejected = source;
            if (countDivergent(heap, source, og, og, 4) != 0 && divergent++ < 10)
                MESSAGE("generated program " << i << " -O" << og << " -g" << og << " differs between allocation states:\n" << source);
            if (hashes.is_open())
            {
                if (og == 0) // the program itself, so a platform difference in the generator shows as such
                    hashes << "generated/" << i << " source " << hex64(source) << "\n";
                hashes << "generated/" << i << " O" << og << " g" << og << " " << pinOf(first) << "\n";
            }
        }
    }
    CHECK_EQ(divergent, 0);
    // the generator makes programs the compiler accepts, or this test checks little
    CHECK_MESSAGE(compiled * 10 >= count * 3 * 9, "the compiler refuses too many generated programs, such as:\n" << rejected);
}

TEST_SUITE_END();
