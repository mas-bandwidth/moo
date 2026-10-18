// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Layer 5 (docs/DESIGN.md "Layers", layer 5): the Luau suite is adapted for fixed, not silenced.
// Every skipped doctest case and every moved conformance script states its reason with "moo:",
// and a reason that names a replacement test resolves by grep. tools/moo-check-skips.sh applies
// the same rule over the whole tree and reports the upstream #if 0 blocks it finds; this case
// keeps the moo side of that list honest inside Moo.Test.
#include "doctest.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

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

TEST_SUITE_BEGIN("moo-l5-suite-audit");

// DESIGN.md "Layers", layer 5: every skipped doctest case names its reason. The current base
// has 42 doctest skips; this card adds eleven analyzer removals. The codegen early returns are
// marked the same way.
TEST_CASE("MooL5SkippedCasesNameAReason")
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
                // moo's own test files must never gain an unmarked skip; upstream #if 0 blocks
                // are the known gap moo-check-skips.sh reports for a follow-up.
                bool mooOwned = record.file.rfind("tests/moo/", 0) == 0 || startsWith(fs::path(record.file).filename().string(), "Moo");
                CHECK_MESSAGE(!mooOwned, record.file << ":" << record.line << " is moo's own #if 0 block and has no moo: reason");
            }
        }

        if (!record.reason.empty())
            checkReferences(root, record, reference, failures);
    }

    // moo: the exact current base has 42 named upstream skips; this card adds eleven Analyzer skips, for 53.
    CHECK_MESSAGE(doctestSkips == 53, "expected 53 doctest skip decorators, found " << doctestSkips);
    CHECK_MESSAGE(earlyReturns == 2, "expected 2 marked early returns, found " << earlyReturns);
    CHECK_MESSAGE(unmarkedIfZero == 11, "expected 11 unmarked upstream #if 0 blocks, found " << unmarkedIfZero);
    for (const std::string& failure : failures)
        CHECK_MESSAGE(false, failure);
}

// DESIGN.md "Layers", layer 5: a moved conformance script keeps its upstream lines and says why
// it exists. Its note names the upstream file, and the note's replacement references resolve.
TEST_CASE("MooL5MovedScriptsNameAReason")
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

TEST_SUITE_END();
