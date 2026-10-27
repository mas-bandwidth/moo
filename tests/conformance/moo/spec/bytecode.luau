-- This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
-- moo: Bytecode: the moo bytecode version, the raw Q48.16 number constants, and the refusals of Luau
-- bytecode, an integer constant, an out-of-range number and a vector constant (docs/DESIGN.md
-- "Layer 3 as built", "Literals").
-- Each block below is one rule; MooSpecScripts runs this file and expects "OK".

do
-- moo: the moo bytecode version (docs/history/layer3/moo-l3-bytecode-version.md; docs/DESIGN.md "Literals").
-- The compiler's blob starts with the moo version byte, then the Luau version its format follows; the VM refuses
-- Luau bytecode and an unknown moo version by name, and number constants load as their raw Q48.16 value.
for level = 0, 2 do
    local bytecode = moo_test.compile("return 0.1, -1.5, 137438953472.5", level)

    -- 0x80 | moo version 1, then a Luau format version (7-bit)
    assert(string.byte(bytecode, 1) == 0x81, tostring(string.byte(bytecode, 1)))
    assert(string.byte(bytecode, 2) < 0x80, tostring(string.byte(bytecode, 2)))

    local fn, err = moo_test.load(bytecode, "=bcv")
    assert(fn, err)
    local a, b, c = fn()
    assert(moo_test.raw(a) == "6554", moo_test.raw(a))
    assert(moo_test.raw(b) == "-98304", moo_test.raw(b))
    assert(moo_test.raw(c) == "9007199254773760", moo_test.raw(c))

    -- the same chunk without the moo byte is Luau bytecode: refused by name
    local luau = string.sub(bytecode, 2)
    local nofn, luauerr = moo_test.load(luau, "=bcv")
    assert(nofn == nil)
    assert(luauerr == "bcv: this is Luau bytecode (version " .. string.byte(bytecode, 2) .. "), not moo bytecode", luauerr)

    -- a moo version this VM does not know: refused by name
    local future = string.char(0x82) .. string.sub(bytecode, 2)
    local nofuture, futureerr = moo_test.load(future, "=bcv")
    assert(nofuture == nil)
    assert(futureerr == "bcv: moo bytecode version mismatch (expected [1..1], got 2)", futureerr)
end
end

do
-- moo: the bytecode constant for a vector is refused by name (docs/history/layer3/moo-l3-vector-constant-refused.md; docs/DESIGN.md
-- "vec3, quat and transform"). The builder never writes LBC_CONSTANT_VECTOR (7) or LBC_CONSTANT_VECTORD (11), and the VM
-- refuses a hand-built moo chunk holding either tag (Common/include/Luau/Bytecode.h "Types of constant table entries"), before
-- it reads the payload.

-- The body of a hand-built chunk in Luau version 9 format (LBC_VERSION_TARGET at BASE), types version 3: no strings, no
-- userdata types, one proto with no instructions (the constant is read before any instruction runs), holding `constant`.
local function chunkWithConstant(constant)
    local s = string.char(0x81, 9, 3, 0, 0, 1) -- moo version 1, Luau version, types version, strings, userdata, protos
    s = s .. string.char(1, 0, 0, 0, 0, 0)     -- maxstacksize, numparams, nups, is_vararg, flags, type info size
    s = s .. string.char(0)                     -- sizecode: no instructions
    s = s .. string.char(1)                     -- sizek
    s = s .. constant
    s = s .. string.char(0)                     -- sizep
    s = s .. string.char(0, 0, 0, 0)            -- linedefined, debugname, lineinfo, debuginfo
    s = s .. string.char(0)                     -- main proto
    return s
end

-- LBC_CONSTANT_VECTOR (7) followed by sixteen bytes of float32 payload; the refusal fires before they are read
local fn, err = moo_test.load(chunkWithConstant(string.char(7) .. string.rep(string.char(0), 16)), "=vc")
assert(fn == nil)
assert(err == "vc: moo bytecode has no vector constant", err)

-- LBC_CONSTANT_VECTORD (11) followed by thirty-two bytes of float64 payload; the refusal fires before they are read
local dnf, derr = moo_test.load(chunkWithConstant(string.char(11) .. string.rep(string.char(0), 32)), "=vcd")
assert(dnf == nil)
assert(derr == "vcd: moo bytecode has no vector constant", derr)
end

return "OK"
