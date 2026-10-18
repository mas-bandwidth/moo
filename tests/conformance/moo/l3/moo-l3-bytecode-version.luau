-- moo: the moo bytecode version (docs/layer3/moo-l3-bytecode-version.md; docs/DESIGN.md "Literals").
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

return "OK"
