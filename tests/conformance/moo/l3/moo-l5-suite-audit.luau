-- moo: samples the reason rule of tools/moo-check-skips.sh (docs/DESIGN.md "Layers", layer 5).
-- A skip reason starts with "moo:"; a "See" reference names a tests/ path, optionally a case,
-- and it must resolve, while a line that says "Replaces upstream" names the file moo removed.
-- The C++ gate that resolves the references is MooL5SkippedCasesNameAReason in
-- tests/moo/moo-l5-suite-audit.test.cpp.
local reasons = {
    "moo: skipped; it asserts folding moo changed on purpose. See tests/MooCompiler.test.cpp",
    "moo: skipped; it asserts hex literals beyond 2^47 parse as doubles (tests/MooNumber.test.cpp LiteralsAreExact)",
    "moo: skipped; it asserts the precision lint on hex literals beyond 2^53",
}

-- "moo:" and a non-empty reason is the first half of the rule.
for _, reason in reasons do
    local at = string.find(reason, "moo:", 1, true)
    assert(at == 1, reason)
    assert(string.find(reason, "%S", at + 4), reason)
end

-- Replacements are the tests/ paths not introduced by "upstream" or "Replaces".
local function references(reason)
    local found = {}
    local at = 1
    while true do
        local first, last = string.find(reason, "tests/[%w_./-]+", at)
        if not first then
            break
        end
        table.insert(found, string.sub(reason, first, last))
        at = last + 1
    end
    return found
end

local function replacements(reason)
    if string.find(reason, "upstream", 1, true) or string.find(reason, "Replaces", 1, true) then
        return {}
    end
    return references(reason)
end

local first = replacements(reasons[1])
assert(#first == 1 and first[1] == "tests/MooCompiler.test.cpp", #first)

local second = replacements(reasons[2])
assert(#second == 1 and second[1] == "tests/MooNumber.test.cpp", second[1])
local _, _, name = string.find(reasons[2], "tests/[%w_./-]+%s+(%u[%w_]*)")
assert(name == "LiteralsAreExact", tostring(name))

assert(#replacements(reasons[3]) == 0, "a reason with no tests/ path names no replacement")

-- An upstream origin is not a replacement reference: the checker skips that line.
local origin = "moo: Replaces upstream tests/conformance/datetime.luau"
assert(#replacements(origin) == 0, "the checker skips this line because it says upstream")

return "OK"
