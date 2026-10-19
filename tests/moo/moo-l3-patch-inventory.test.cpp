// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// The patch inventory is the merge checklist (docs/DESIGN.md "Upstream merges"): docs/PATCHES.md
// has one row per upstream file moo modifies or deletes, and the push gate runs
// tools/moo-check-patches.sh against the fork commit. This case keeps the file's shape honest in
// Moo.Test. The set comparison itself needs full history, so it is the script's job in the push
// gate (the nightly Moo.Test checkout is shallow).
#include "doctest.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{

namespace fs = std::filesystem;

fs::path mooRoot()
{
#ifdef LUAU_CONFORMANCE_SOURCE_DIR
    return fs::path(LUAU_CONFORMANCE_SOURCE_DIR).parent_path().parent_path();
#else
    return fs::current_path();
#endif
}

std::vector<std::string> readLines(const fs::path& file)
{
    std::ifstream input(file);
    REQUIRE_MESSAGE(input.good(), "cannot read " << file);

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line))
        lines.push_back(line);
    return lines;
}

bool startsWith(const std::string& text, const char* prefix)
{
    return text.compare(0, std::char_traits<char>::length(prefix), prefix) == 0;
}

// The first backticked token on a line, or "" when there is none.
std::string firstBacktick(const std::string& line)
{
    size_t open = line.find('`');
    if (open == std::string::npos)
        return "";
    size_t close = line.find('`', open + 1);
    if (close == std::string::npos)
        return "";
    return line.substr(open + 1, close - open - 1);
}

struct PatchRow
{
    std::string area;
    std::string path;
};

} // namespace

TEST_SUITE_BEGIN("moo-l3-patch-inventory");

// DESIGN.md "Upstream merges": every row names an upstream file and a reason, and the rows are
// grouped by area. A row's reason must cite the document section the change implements.
TEST_CASE("MooL3PatchInventoryRows")
{
    fs::path root = mooRoot();
    fs::path inventory = root / "docs" / "PATCHES.md";
    std::vector<std::string> lines = readLines(inventory);

    std::vector<PatchRow> rows;
    std::string area;
    bool inAdded = false;

    for (const std::string& line : lines)
    {
        if (startsWith(line, "## moo-added files"))
        {
            inAdded = true;
            area.clear();
            continue;
        }
        if (startsWith(line, "## "))
        {
            inAdded = false;
            area.clear();
            continue;
        }
        if (inAdded)
            continue;
        if (startsWith(line, "### "))
        {
            area = line.substr(4);
            continue;
        }
        if (!startsWith(line, "| `"))
            continue;

        std::string path = firstBacktick(line);
        REQUIRE_MESSAGE(!path.empty(), "a table row has no backticked path: " << line);
        CHECK_MESSAGE(!area.empty(), path << " is not under an area heading");
        bool citesSection = line.find("DESIGN.md") != std::string::npos || line.find("DETERMINISM.md") != std::string::npos;
        CHECK_MESSAGE(citesSection, path << " has no reason naming a DESIGN.md or DETERMINISM.md section");
        rows.push_back({area, path});
    }

    CHECK_MESSAGE(rows.size() >= 91, "expected at least the 91 upstream files moo changes at layer 3, found " << rows.size());

    std::set<std::string> paths;
    std::set<std::string> areas;
    for (const PatchRow& row : rows)
    {
        CHECK_MESSAGE(paths.insert(row.path).second, row.path << " is listed twice");
        areas.insert(row.area);
    }

    const char* const expected[] = {"VM", "Compiler", "Ast", "Analysis", "Bytecode", "CLI", "Config", "CMake/Makefile", "fuzz", "tests", "workflows", "README"};
    for (const char* name : expected)
        CHECK_MESSAGE(areas.count(name) == 1, "no rows under the area " << name);
}

// DESIGN.md "Upstream merges": the push gate runs the script, and the checkout keeps the fork
// commit in history so the script can compare against it. moo-added files are listed by directory.
TEST_CASE("MooL3PatchInventoryGate")
{
    fs::path root = mooRoot();
    fs::path script = root / "tools" / "moo-check-patches.sh";
    fs::path ci = root / ".github" / "workflows" / "ci.yml";

    REQUIRE_MESSAGE(fs::exists(script), "missing " << script);
    fs::perms permissions = fs::status(script).permissions();
    CHECK_MESSAGE((permissions & fs::perms::owner_exec) != fs::perms::none, script << " is not executable");

    std::ifstream scriptFile(script);
    std::stringstream scriptText;
    scriptText << scriptFile.rdbuf();
    CHECK_MESSAGE(scriptText.str().find("2d88f9b5facaa9e4ec91247b39ae8251bd49a244") != std::string::npos,
        "the script does not name the Luau 0.741 fork commit");

    std::ifstream ciFile(ci);
    std::stringstream ciText;
    ciText << ciFile.rdbuf();
    CHECK_MESSAGE(ciText.str().find("fetch-depth: 0") != std::string::npos, "the push gate does not fetch full history");
    CHECK_MESSAGE(ciText.str().find("tools/moo-check-patches.sh") != std::string::npos, "the push gate does not run the patch check");

    // The added-files section is by directory, and every named path exists in the tree.
    std::vector<std::string> lines = readLines(root / "docs" / "PATCHES.md");
    bool inAdded = false;
    int added = 0;
    for (const std::string& line : lines)
    {
        if (startsWith(line, "## moo-added files"))
        {
            inAdded = true;
            continue;
        }
        if (inAdded && startsWith(line, "## "))
            break;
        if (!inAdded || !startsWith(line, "- `"))
            continue;

        std::string path = firstBacktick(line);
        REQUIRE_MESSAGE(!path.empty(), "an added-files bullet has no path: " << line);
        CHECK_MESSAGE(fs::exists(root / path), "the added-files list names " << path << ", which does not exist");
        added++;
    }
    CHECK_MESSAGE(added >= 15, "expected the added files grouped by directory, found " << added << " entries");
}

TEST_SUITE_END();
