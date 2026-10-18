// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: the analyzer removes vector and integer from embedded builtins (docs/DESIGN.md "Analyzer");
// pin the analyzer-visible behavior replacing the skipped upstream assertions.
#include "Luau/ConfigResolver.h"
#include "Luau/FileResolver.h"
#include "Luau/Frontend.h"
#include "Luau/ToString.h"

#include "doctest.h"

#include <string>
#include <unordered_map>

using namespace Luau;

namespace
{
struct InlineFileResolver : NullFileResolver
{
    std::unordered_map<ModuleName, std::string> sources;

    std::optional<SourceCode> readSource(const ModuleName& name) override
    {
        auto it = sources.find(name);
        if (it == sources.end())
            return std::nullopt;
        return SourceCode{it->second, SourceCode::Module};
    }
};
} // namespace

TEST_SUITE_BEGIN("moo-l3-unittest-base-reds-named");

TEST_CASE("RemovedVectorAndIntegerAreUnknownAnalyzerGlobals")
{
    InlineFileResolver files;
    NullConfigResolver config;
    Frontend frontend(&files, &config);
    files.sources["Test"] = "--!strict\nlocal v = vector\nlocal i = integer\n";

    CheckResult result = frontend.check("Test");
    REQUIRE(result.errors.size() == 2);
    CHECK(toString(result.errors[0]) == "Unknown global 'vector'; consider assigning to it first");
    CHECK(toString(result.errors[1]) == "Unknown global 'integer'; consider assigning to it first");
}

TEST_SUITE_END();
