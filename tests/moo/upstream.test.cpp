// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo's record of its upstream changes: every skipped or moved upstream test names a reason, and
// docs/PATCHES.md lists every changed upstream file (docs/DESIGN.md "Upstream merges").

#include "doctest.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#include <set>

TEST_SUITE_BEGIN("upstream");

// Layer 5 (docs/DESIGN.md "Layers", layer 5): the Luau suite is adapted for fixed, not silenced.
// Every skipped doctest case and every moved conformance script states its reason with "moo:",
// and a reason that names a replacement test resolves by grep. tools/moo-check-skips.sh applies
// the same rule over the whole tree, upstream #if 0 blocks included; this case keeps that list
// honest inside Moo.Test.
namespace skips
{

namespace
{

namespace fs = std::filesystem;

struct SkipRecord
{
    std::string file;
    int line = 0;
    std::string kind;
    std::string reason;
};

std::string trim(const std::string& text)
{
    size_t first = 0;
    while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first])))
        first++;
    size_t last = text.size();
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1])))
        last--;
    return text.substr(first, last - first);
}

bool startsWith(const std::string& text, const char* prefix)
{
    return text.compare(0, std::char_traits<char>::length(prefix), prefix) == 0;
}

bool isWordAfter(const std::string& text, const char* word)
{
    if (!startsWith(text, word))
        return false;
    size_t end = std::char_traits<char>::length(word);
    return end == text.size() || std::isspace(static_cast<unsigned char>(text[end]));
}

bool isIfZero(const std::string& line)
{
    std::string text = trim(line);
    if (!startsWith(text, "#if"))
        return false;
    size_t at = 3;
    while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at])))
        at++;
    return at < text.size() && text[at] == '0' && (at + 1 == text.size() || std::isspace(static_cast<unsigned char>(text[at + 1])));
}

bool isAnyIf(const std::string& line)
{
    std::string text = trim(line);
    return isWordAfter(text, "#if") || isWordAfter(text, "#ifdef") || isWordAfter(text, "#ifndef");
}

bool isEndif(const std::string& line)
{
    return isWordAfter(trim(line), "#endif");
}

bool isEarlyReturn(const std::string& line)
{
    return trim(line) == "return;";
}

bool isIf(const std::string& line)
{
    std::string text = trim(line);
    return startsWith(text, "if ") || startsWith(text, "if(");
}

bool hasReason(const std::string& line)
{
    return line.find("moo:") != std::string::npos;
}

std::string reasonFor(const std::string& line, const std::string& above)
{
    if (hasReason(line))
        return line;
    if (hasReason(above))
        return above;
    return "";
}

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

std::vector<SkipRecord> scanTests(const fs::path& root)
{
    std::vector<SkipRecord> records;
    std::error_code error;

    fs::path tests = root / "tests";
    for (fs::recursive_directory_iterator it(tests, error), end; !error && it != end; it.increment(error))
    {
        if (!it->is_regular_file() || it->path().extension() != ".cpp")
            continue;

        std::vector<std::string> lines = readLines(it->path());
        std::string relative = it->path().lexically_relative(root).generic_string();

        bool inBlock = false;
        int depth = 0;
        int blockLine = 0;
        bool blockHasCase = false;
        std::string blockReason;
        std::string previous;
        std::string previous2;

        for (size_t index = 0; index < lines.size(); ++index)
        {
            const std::string& line = lines[index];
            int number = int(index) + 1;

            if (inBlock)
            {
                if (isAnyIf(line))
                    depth++;
                if (isEndif(line))
                {
                    depth--;
                    if (depth == 0)
                    {
                        if (blockHasCase)
                            records.push_back({relative, blockLine, "#if 0 block", blockReason});
                        inBlock = false;
                    }
                }
                if (startsWith(trim(line), "TEST_CASE"))
                    blockHasCase = true;

                previous2 = previous;
                previous = line;
                continue;
            }

            if (isIfZero(line))
            {
                inBlock = true;
                depth = 1;
                blockLine = number;
                blockHasCase = false;
                blockReason = reasonFor(line, previous);

                previous2 = previous;
                previous = line;
                continue;
            }

            if (line.find("doctest::ski" "p") != std::string::npos)
                records.push_back({relative, number, "doctest skip", reasonFor(line, previous)});

            // The codegen cases moo excludes say so above the if; the early return is the skip.
            if (isEarlyReturn(line) && isIf(previous))
            {
                std::string reason = hasReason(previous) ? previous : (hasReason(previous2) ? previous2 : "");
                if (!reason.empty() || hasReason(line))
                    records.push_back({relative, number - 1, "early return", reason.empty() ? line : reason});
            }

            previous2 = previous;
            previous = line;
        }
    }

    fs::path conformance = root / "tests" / "conformance" / "moo";
    for (fs::recursive_directory_iterator it(conformance, error), end; !error && it != end; it.increment(error))
    {
        if (!it->is_regular_file() || it->path().extension() != ".luau")
            continue;

        std::vector<std::string> lines = readLines(it->path());
        std::string relative = it->path().lexically_relative(root).generic_string();
        std::string reason;
        bool marked = false;
        for (const std::string& line : lines)
        {
            std::string comment = trim(line);
            if (!startsWith(comment, "--"))
                continue;
            if (comment.find("moo:") != std::string::npos || comment.find("moo-specific") != std::string::npos)
                marked = true;
            reason += comment + "\n";
        }
        records.push_back({relative, 0, "moved script", marked ? reason : ""});
    }

    return records;
}

// A reason that names a replacement must resolve. "Replaces upstream tests/X" names the file
// that is gone, so those lines are not replacement references (docs/DETERMINISM.md "Status").
void checkReferences(const fs::path& root, const SkipRecord& record, const std::regex& pattern, std::vector<std::string>& failures)
{
    std::istringstream input(record.reason);
    std::string line;
    while (std::getline(input, line))
    {
        if (line.find("upstream") != std::string::npos || line.find("Upstream") != std::string::npos ||
            line.find("Replaces") != std::string::npos)
            continue;

        for (std::sregex_iterator it(line.begin(), line.end(), pattern), end; it != end; ++it)
        {
            std::string path = (*it)[0].str();
            std::string name = (*it)[1].matched ? (*it)[1].str() : "";
            if (!name.empty())
            {
                path.erase(path.size() - name.size());
                path = trim(path);
            }
            while (!path.empty() && (path.back() == '.' || path.back() == ',' || path.back() == ';' || path.back() == ':' || path.back() == ')'))
                path.pop_back();

            fs::path target = root / path;
            if (!fs::exists(target))
            {
                failures.push_back(record.file + ":" + std::to_string(record.line) + ": " + record.kind + " names " + path +
                                   ", which does not exist");
                continue;
            }

            if (!name.empty())
            {
                std::ifstream inputFile(target);
                std::stringstream contents;
                contents << inputFile.rdbuf();
                if (contents.str().find(name) == std::string::npos)
                    failures.push_back(record.file + ":" + std::to_string(record.line) + ": " + record.kind + " names " + name +
                                       ", which grep does not find in " + path);
            }
        }
    }
}

} // namespace


// DESIGN.md "Layers", layer 5: every skipped doctest case names its reason. DETERMINISM.md
// "Status" lists the nine parser/linter/pretty-printer/frontend cases and the twenty compiler
// cases moo skips; the two codegen early returns are marked the same way.
TEST_CASE("UpstreamSkippedCasesNameAReason")
{
    fs::path root = mooRoot();
    std::vector<SkipRecord> records = scanTests(root);

    const std::regex reference(R"(tests/[A-Za-z0-9_./-]+(?: +([A-Z][A-Za-z0-9_]*))?)");
    std::vector<std::string> failures;
    int doctestSkips = 0;
    int earlyReturns = 0;
    int unmarkedIfZero = 0;

    for (const SkipRecord& record : records)
    {
        if (record.kind == "doctest skip")
        {
            doctestSkips++;
            CHECK_MESSAGE(!record.reason.empty(), record.file << ":" << record.line << " has no moo: reason");
        }
        else if (record.kind == "early return")
        {
            earlyReturns++;
            CHECK_MESSAGE(!record.reason.empty(), record.file << ":" << record.line << " has no moo: reason");
        }
        else if (record.kind == "#if 0 block")
        {
            if (record.reason.empty())
            {
                unmarkedIfZero++;
                // every #if 0 block names its reason; moo's own test files are named apart
                bool mooOwned = record.file.rfind("tests/moo/", 0) == 0 || startsWith(fs::path(record.file).filename().string(), "Moo");
                CHECK_MESSAGE(!mooOwned, record.file << ":" << record.line << " is moo's own #if 0 block and has no moo: reason");
            }
        }

        if (!record.reason.empty())
            checkReferences(root, record, reference, failures);
    }

    // moo: current sprint/moo has 45; moo-l3-unittest-base-reds-named adds eleven (docs/history/layer3/moo-l3-unittest-base-reds-named.md)
    CHECK_MESSAGE(doctestSkips == 56, "expected 56 doctest skip decorators, found " << doctestSkips);
    CHECK_MESSAGE(earlyReturns == 2, "expected 2 marked early returns, found " << earlyReturns);
    CHECK_MESSAGE(unmarkedIfZero == 0, "expected no unmarked #if 0 blocks, found " << unmarkedIfZero);
    for (const std::string& failure : failures)
        CHECK_MESSAGE(false, failure);
}

// DESIGN.md "Layers", layer 5: a moved conformance script keeps its upstream lines and says why
// it exists. Its note names the upstream file, and the note's replacement references resolve.
TEST_CASE("UpstreamMovedScriptsNameAReason")
{
    fs::path root = mooRoot();
    std::vector<SkipRecord> records = scanTests(root);

    const std::regex reference(R"(tests/[A-Za-z0-9_./-]+(?: +([A-Z][A-Za-z0-9_]*))?)");
    std::vector<std::string> failures;
    int scripts = 0;

    for (const SkipRecord& record : records)
    {
        if (record.kind != "moved script")
            continue;

        scripts++;
        CHECK_MESSAGE(!record.reason.empty(), record.file << " has no moo: reason");
        if (!record.reason.empty())
            checkReferences(root, record, reference, failures);
    }

    // Later cards add moved scripts; each is held to the reason check above.
    CHECK_MESSAGE(scripts >= 19, "expected at least 19 moved conformance scripts (layer 5 baseline), found " << scripts);
    for (const std::string& failure : failures)
        CHECK_MESSAGE(false, failure);
}

} // namespace skips

// The patch inventory is the merge checklist (docs/DESIGN.md "Upstream merges"): docs/PATCHES.md
// has one row per upstream file moo modifies or deletes, and the push gate runs
// tools/moo-check-patches.sh against the fork commit. This case keeps the file's shape honest in
// Moo.Test. The set comparison itself needs full history, so it is the script's job in the push
// gate (the nightly Moo.Test checkout is shallow).
namespace patches
{

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


// DESIGN.md "Upstream merges": every row names an upstream file and a reason, and the rows are
// grouped by area. A row's reason must cite the document section the change implements.
TEST_CASE("UpstreamPatchInventoryRows")
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
TEST_CASE("UpstreamPatchInventoryGate")
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

} // namespace patches

TEST_SUITE_END();
