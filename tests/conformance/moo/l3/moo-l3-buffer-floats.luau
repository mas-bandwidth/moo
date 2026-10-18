-- moo: buffer.readf32/readf64/writef32/writef64 by the exact boundary rule (docs/DESIGN.md,
-- "Layer 3 as built": the buffer float TEMPORARY bridge is gone; docs/DETERMINISM.md, 18). Reads
-- decode the stored IEEE bits to Q48.16 with the one boundary rule (ties to even); a NaN, infinity,
-- or out-of-range value raises a named error. Writes encode Q48.16 to the nearest float bit pattern
-- in integer arithmetic (moo_numtofloatbits / moo_numtodoublebits).

local step = moo_test.fromraw("1")      -- 2^-16
local half = moo_test.fromraw("-32768") -- -0.5

local function roundtrip(fmt, value, expectedRaw)
    local b = buffer.create(8)
    buffer["write" .. fmt](b, 0, value)
    local got = buffer["read" .. fmt](b, 0)
    assert(moo_test.raw(got) == expectedRaw, fmt .. " round trip: got " ..
        moo_test.raw(got) .. " want " .. expectedRaw)
end

-- 0.1 is raw 6554 in Q48.16, exactly representable in both formats, so it round-trips exactly.
roundtrip("f32", moo_test.fromraw("6554"), "6554")
roundtrip("f64", moo_test.fromraw("6554"), "6554")
roundtrip("f32", half, "-32768")
roundtrip("f64", half, "-32768")
roundtrip("f32", step, "1")
roundtrip("f64", step, "1")

-- The largest value (2^47 - 2^-16) encodes to the float 2^47 on the wire; decoding that back is
-- outside the number range, so reading it raises the named out-of-range error.
for _, fmt in ipairs({"f32", "f64"}) do
    local b = buffer.create(8)
    buffer["write" .. fmt](b, 0, moo_test.fromraw("9223372036854775807"))
    local ok, err = pcall(function() return buffer["read" .. fmt](b, 0) end)
    assert(not ok, fmt .. " largest value read must error")
    assert(string.find(err, "buffer float out of number range", 1, true), fmt .. " err: " .. tostring(err))
end

-- Ties: a stored double exactly halfway between raw 16384 and 16385 (value (2^15+1) * 2^-17)
-- reads back to the even raw 16384. Binary64 bit patterns bigger than the integer literal range are
-- written as two 32-bit halves (low at bit 0, high at bit 32). 0x3FD0000002000000 = (0x3FD00000
-- << 32) | 0x02000000.
local b = buffer.create(8)
buffer.writebits(b, 0, 32, 0x02000000)
buffer.writebits(b, 32, 32, 0x3FD00000)
assert(moo_test.raw(buffer.readf64(b, 0)) == "16384", moo_test.raw(buffer.readf64(b, 0)))

-- The three named errors, one per moo.h status (NaN, infinite, out of range), for both formats.
local f32patterns = {
    { 0x7FC00000, "buffer float is NaN" },
    { 0x7F800000, "buffer float is infinite" },
    { 0x57000000, "buffer float out of number range" },
}
for _, p in ipairs(f32patterns) do
    local buf = buffer.create(8)
    buffer.writebits(buf, 0, 32, p[1])
    local ok, err = pcall(function() return buffer.readf32(buf, 0) end)
    assert(not ok, "f32 should error")
    assert(string.find(err, p[2], 1, true), "f32 err: " .. tostring(err))
end

local f64patterns = {
    { 0x00000000, 0x7FF80000, "buffer float is NaN" },
    { 0x00000000, 0x7FF00000, "buffer float is infinite" },
    { 0x00000000, 0x42E00000, "buffer float out of number range" },
}
for _, p in ipairs(f64patterns) do
    local buf = buffer.create(8)
    buffer.writebits(buf, 0, 32, p[1])
    buffer.writebits(buf, 32, 32, p[2])
    local ok, err = pcall(function() return buffer.readf64(buf, 0) end)
    assert(not ok, "f64 should error")
    assert(string.find(err, p[3], 1, true), "f64 err: " .. tostring(err))
end

return "OK"
