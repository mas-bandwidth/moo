-- moo: pin string.pack/unpack f, d and n by the exact boundary rule
-- (docs/DESIGN.md "Boundary", DETERMINISM.md 18). f packs the nearest float bit pattern
-- and unpacks its IEEE bits; d does the same for double; n is the raw Q48.16 int64, an exact
-- round trip. Both honour the '<'/'>' endianness option. Unpacking a NaN, infinity or
-- out-of-range value raises a named error. Returns "OK".

for _, e in ipairs({ "<", ">" }) do
    -- d and n are exact round trips (both 8-byte; n is the raw int64)
    for _, v in ipairs({ 0, 1, -1, 1.5, -2.25, 65536, 123.456 }) do
        local packed = string.pack(e .. "d", v)
        assert(#packed == 8)
        local back = string.unpack(e .. "d", packed)
        assert(back == v, tostring(v) .. " d -> " .. tostring(back))

        local packedn = string.pack(e .. "n", v)
        assert(#packedn == 8)
        local backn = string.unpack(e .. "n", packedn)
        assert(backn == v, tostring(v) .. " n -> " .. tostring(backn))

        -- the raw values agree across d and n when the double is exact
        assert(moo_test.raw(backn) == moo_test.raw(back), moo_test.raw(backn) .. " vs " .. moo_test.raw(back))
    end

    -- f round trips of exactly-representable float32 values
    for _, v in ipairs({ 0, 1, -1, 0.5, -2.25, 65536, 141.25 }) do
        local packed = string.pack(e .. "f", v)
        assert(#packed == 4)
        local back = string.unpack(e .. "f", packed)
        assert(back == v, tostring(v) .. " f -> " .. tostring(back))
    end
end

-- ties to even: raw 16777217 is exactly halfway between float 256.0 and its successor, so it
-- rounds to 256.0 (even mantissa), whose little-endian bytes are 00 00 80 43.
local tie = string.pack("<f", moo_test.fromraw("16777217"))
assert(#tie == 4)
assert(string.byte(tie, 1) == 0 and string.byte(tie, 2) == 0)
assert(string.byte(tie, 3) == 0x80 and string.byte(tie, 4) == 0x43)

-- named unpack errors for f and d: NaN, infinity, out of range (byte strings are little-endian)
local function unpackErr(fmt, bytes)
    local ok, err = pcall(string.unpack, fmt, bytes)
    assert(not ok, "expected an error")
    return err
end

-- d: 0x7FF0000000000001 (NaN), 0x7FF0000000000000 (inf), 0x4330000000000000 (2^48)
assert(unpackErr("<d", "\1\0\0\0\0\0\240\127") == "string.unpack float is NaN")
assert(unpackErr("<d", "\0\0\0\0\0\0\240\127") == "string.unpack float is infinite")
assert(unpackErr("<d", "\0\0\0\0\0\0\48\67") == "string.unpack float out of number range")

-- f: 0x7FC00000 (NaN), 0x7F800000 (inf), 0x57000000 (2^47)
assert(unpackErr("<f", "\0\0\192\127") == "string.unpack float is NaN")
assert(unpackErr("<f", "\0\0\128\127") == "string.unpack float is infinite")
assert(unpackErr("<f", "\0\0\0\87") == "string.unpack float out of number range")

return "OK"
