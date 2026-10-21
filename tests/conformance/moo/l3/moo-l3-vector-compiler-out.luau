-- moo: the compiler no longer recognises vector.* or vectorLib/vectorCtor as builtins and never folds a vector
-- (docs/layer3/moo-l3-vector-compiler-out.md; docs/DESIGN.md "vec3, quat and transform"; docs/DETERMINISM.md 17).

local LOP_GETGLOBAL = 7
local LOP_GETIMPORT = 12
local LOP_GETTABLEKS = 15
local LOP_FASTCALL3 = 60
local LOP_FASTCALL = 68
local LOP_FASTCALL1 = 73
local LOP_FASTCALL2 = 74
local LOP_FASTCALL2K = 75
local LBC_CONSTANT_VECTOR = 7
local LBC_CONSTANT_VECTORD = 11

-- Read unsigned LEB128 from bytecode at 1-based pos; returns value, nextPos.
local function readVarInt(bytes, pos)
    local result = 0
    local shift = 1
    while true do
        local b = string.byte(bytes, pos)
        pos = pos + 1
        result = result + (b % 128) * shift
        if b < 128 then
            return result, pos
        end
        shift = shift * 128
    end
end

local function hasAuxWord(op)
    return op == LOP_GETGLOBAL
        or op == LOP_GETIMPORT
        or op == LOP_GETTABLEKS
        or op == LOP_FASTCALL2
        or op == LOP_FASTCALL2K
        or op == LOP_FASTCALL3
end

-- Parse a single-proto moo bytecode blob and return its opcode list and constant tags.
local function parseMainProto(bytes)
    assert(string.byte(bytes, 1) == 0x81, "expected moo bytecode v1")
    local pos = 4 -- skip moo version (1), luau version (2), types version (3)

    local stringCount
    stringCount, pos = readVarInt(bytes, pos)
    for _ = 1, stringCount do
        local len
        len, pos = readVarInt(bytes, pos)
        pos = pos + len
    end

    -- userdata type remapping terminated by 0
    while true do
        local idx = string.byte(bytes, pos)
        pos = pos + 1
        if idx == 0 then
            break
        end
        local _
        _, pos = readVarInt(bytes, pos)
    end

    local protoCount
    protoCount, pos = readVarInt(bytes, pos)
    assert(protoCount == 1, "expected 1 proto")

    -- maxstacksize, numparams, nups, is_vararg, flags (5 bytes)
    pos = pos + 5

    local typesize
    typesize, pos = readVarInt(bytes, pos)
    pos = pos + typesize

    local sizecode
    sizecode, pos = readVarInt(bytes, pos)
    local opcodes = {}
    local idx = 1
    while idx <= sizecode do
        local op = string.byte(bytes, pos)
        table.insert(opcodes, op)
        if hasAuxWord(op) then
            pos = pos + 8
            idx = idx + 2
        else
            pos = pos + 4
            idx = idx + 1
        end
    end

    local sizek
    sizek, pos = readVarInt(bytes, pos)
    local ktags = {}
    for _ = 1, sizek do
        local kt = string.byte(bytes, pos)
        pos = pos + 1
        table.insert(ktags, kt)
        if kt == 0 then
            -- nil
        elseif kt == 1 then
            pos = pos + 1
        elseif kt == 2 or kt == 9 then
            pos = pos + 8
        elseif kt == 3 then
            local _, nextPos = readVarInt(bytes, pos)
            pos = nextPos
        elseif kt == 4 then
            pos = pos + 4
        elseif kt == 5 then
            local keys
            keys, pos = readVarInt(bytes, pos)
            for _ = 1, keys do
                _, pos = readVarInt(bytes, pos)
            end
        elseif kt == 6 then
            local _, nextPos = readVarInt(bytes, pos)
            pos = nextPos
        elseif kt == 7 then
            pos = pos + 16
        elseif kt == 11 then
            pos = pos + 32
        else
            error("unexpected constant tag " .. tostring(kt))
        end
    end

    return opcodes, ktags
end

local sources = {
    "return vector.create(1, 2, 3)",
    "return vector.magnitude(x)",
    "return vector.normalize(x)",
    "return vector.cross(x, y)",
    "return vector.dot(x, y)",
    "return vector.floor(x)",
    "return vector.ceil(x)",
    "return vector.abs(x)",
    "return vector.sign(x)",
    "return vector.clamp(x, a, b)",
    "return vector.min(x, y)",
    "return vector.max(x, y)",
    "return vector.lerp(x, y, t)",
}

for _, src in ipairs(sources) do
    for level = 0, 2 do
        local bc = moo_test.compile(src, level)
        local opcodes, ktags = parseMainProto(bc)

        local hasGetGlobalOrImport = false
        for _, op in ipairs(opcodes) do
            if op == LOP_GETIMPORT or op == LOP_GETGLOBAL then
                hasGetGlobalOrImport = true
            end
            assert(
                op ~= LOP_FASTCALL and op ~= LOP_FASTCALL1 and op ~= LOP_FASTCALL2 and op ~= LOP_FASTCALL2K and op ~= LOP_FASTCALL3,
                src .. " at -O" .. tostring(level) .. " emitted FASTCALL opcode " .. tostring(op)
            )
        end
        assert(hasGetGlobalOrImport, src .. " at -O" .. tostring(level) .. " missing GETIMPORT/GETGLOBAL")

        for _, kt in ipairs(ktags) do
            assert(
                kt ~= LBC_CONSTANT_VECTOR and kt ~= LBC_CONSTANT_VECTORD,
                src .. " at -O" .. tostring(level) .. " emitted vector constant tag " .. tostring(kt)
            )
        end
    end
end

return "OK"
