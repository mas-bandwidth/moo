-- This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
-- moo: Numbers: Q48.16 arithmetic, the named overflow errors of a checked build, and the exact-integer
-- rule for integer arguments, on the library and the fastcall paths (docs/DESIGN.md "Representation",
-- "Overflow", "Exact integers").
-- Each block below is one rule; MooSpecScripts runs this file and expects "OK".

do
    -- moo: DESIGN.md "Exact integers": every fastcall-able builtin that takes an integer checks
    -- luai_numisint first and falls back to the library path on a fraction, so -O0, -O1 and -O2
    -- raise "number has no integer representation" on 1.5 and return the same result on 2.
    local cases = {
        -- bit32.*
        {"bit32.arshift arg 1", "return bit32.arshift(1.5, 2)", "invalid argument #1 to 'arshift' (number has no integer representation)", "return bit32.arshift(8, 2)", "2"},
        {"bit32.arshift arg 2", "return bit32.arshift(8, 1.5)", "invalid argument #2 to 'arshift' (number has no integer representation)", "return bit32.arshift(2, 1)", "1"},
        {"bit32.band arg 1", "return bit32.band(1.5, 2)", "invalid argument #1 to 'band' (number has no integer representation)", "return bit32.band(3, 2)", "2"},
        {"bit32.band arg 2", "return bit32.band(3, 1.5)", "invalid argument #2 to 'band' (number has no integer representation)", "return bit32.band(2, 3)", "2"},
        {"bit32.band arg 3", "return bit32.band(7, 3, 1.5)", "invalid argument #3 to 'band' (number has no integer representation)", "return bit32.band(7, 3, 2)", "2"},
        {"bit32.bnot", "return bit32.bnot(1.5)", "invalid argument #1 to 'bnot' (number has no integer representation)", "return bit32.bnot(2)", "4294967293"},
        {"bit32.bor arg 1", "return bit32.bor(1.5, 2)", "invalid argument #1 to 'bor' (number has no integer representation)", "return bit32.bor(1, 2)", "3"},
        {"bit32.bor arg 2", "return bit32.bor(1, 1.5)", "invalid argument #2 to 'bor' (number has no integer representation)", "return bit32.bor(2, 4)", "6"},
        {"bit32.bor arg 3", "return bit32.bor(1, 2, 1.5)", "invalid argument #3 to 'bor' (number has no integer representation)", "return bit32.bor(1, 4, 2)", "7"},
        {"bit32.bxor arg 1", "return bit32.bxor(1.5, 2)", "invalid argument #1 to 'bxor' (number has no integer representation)", "return bit32.bxor(3, 2)", "1"},
        {"bit32.bxor arg 2", "return bit32.bxor(3, 1.5)", "invalid argument #2 to 'bxor' (number has no integer representation)", "return bit32.bxor(2, 1)", "3"},
        {"bit32.bxor arg 3", "return bit32.bxor(7, 1, 1.5)", "invalid argument #3 to 'bxor' (number has no integer representation)", "return bit32.bxor(7, 1, 2)", "4"},
        {"bit32.btest arg 1", "return bit32.btest(1.5, 2)", "invalid argument #1 to 'btest' (number has no integer representation)", "return bit32.btest(3, 2)", "true"},
        {"bit32.btest arg 2", "return bit32.btest(3, 1.5)", "invalid argument #2 to 'btest' (number has no integer representation)", "return bit32.btest(2, 1)", "false"},
        {"bit32.btest arg 3", "return bit32.btest(7, 3, 1.5)", "invalid argument #3 to 'btest' (number has no integer representation)", "return bit32.btest(7, 3, 2)", "true"},
        {"bit32.extract 2-arg arg 1", "local f = 1 return bit32.extract(1.5, f)", "invalid argument #1 to 'extract' (number has no integer representation)", "local f = 1 return bit32.extract(2, f)", "1"},
        {"bit32.extract 2-arg arg 2", "return bit32.extract(6, 1.5)", "invalid argument #2 to 'extract' (number has no integer representation)", "return bit32.extract(6, 2)", "1"},
        {"bit32.extract 3-arg nonconst arg 2", "local f, w = 1.5, 2 f, w = f, w return bit32.extract(6, f, w)", "invalid argument #2 to 'extract' (number has no integer representation)", "local f, w = 2, 2 f, w = f, w return bit32.extract(12, f, w)", "3"},
        {"bit32.extract 3-arg nonconst arg 3", "local f, w = 1, 1.5 f, w = f, w return bit32.extract(6, f, w)", "invalid argument #3 to 'extract' (number has no integer representation)", "local f, w = 1, 2 f, w = f, w return bit32.extract(6, f, w)", "3"},
        {"bit32.extract FASTCALL2K arg 1", "return bit32.extract(1.5, 1, 2)", "invalid argument #1 to 'extract' (number has no integer representation)", "return bit32.extract(6, 1, 2)", "3"},
        {"bit32.lrotate arg 1", "return bit32.lrotate(1.5, 2)", "invalid argument #1 to 'lrotate' (number has no integer representation)", "return bit32.lrotate(1, 2)", "4"},
        {"bit32.lrotate arg 2", "return bit32.lrotate(1, 1.5)", "invalid argument #2 to 'lrotate' (number has no integer representation)", "return bit32.lrotate(2, 1)", "4"},
        {"bit32.lshift arg 1", "return bit32.lshift(1.5, 2)", "invalid argument #1 to 'lshift' (number has no integer representation)", "return bit32.lshift(1, 2)", "4"},
        {"bit32.lshift arg 2", "return bit32.lshift(1, 1.5)", "invalid argument #2 to 'lshift' (number has no integer representation)", "return bit32.lshift(2, 1)", "4"},
        {"bit32.replace 3-arg arg 1", "local v = 1 return bit32.replace(1.5, v, 2)", "invalid argument #1 to 'replace' (number has no integer representation)", "local v = 1 return bit32.replace(0, v, 2)", "4"},
        {"bit32.replace 3-arg arg 2", "local n = 0 return bit32.replace(n, 1.5, 2)", "invalid argument #2 to 'replace' (number has no integer representation)", "local n = 0 return bit32.replace(n, 2, 1)", "0"},
        {"bit32.replace 3-arg arg 3", "local n = 0 return bit32.replace(n, 1, 1.5)", "invalid argument #3 to 'replace' (number has no integer representation)", "local n = 0 return bit32.replace(n, 1, 2)", "4"},
        {"bit32.replace 4-arg arg 4", "return bit32.replace(0, 3, 1, 1.5)", "invalid argument #4 to 'replace' (number has no integer representation)", "return bit32.replace(0, 3, 1, 2)", "6"},
        {"bit32.rrotate arg 1", "return bit32.rrotate(1.5, 2)", "invalid argument #1 to 'rrotate' (number has no integer representation)", "return bit32.rrotate(8, 2)", "2"},
        {"bit32.rrotate arg 2", "return bit32.rrotate(8, 1.5)", "invalid argument #2 to 'rrotate' (number has no integer representation)", "return bit32.rrotate(4, 2)", "1"},
        {"bit32.rshift arg 1", "return bit32.rshift(1.5, 2)", "invalid argument #1 to 'rshift' (number has no integer representation)", "return bit32.rshift(8, 2)", "2"},
        {"bit32.rshift arg 2", "return bit32.rshift(8, 1.5)", "invalid argument #2 to 'rshift' (number has no integer representation)", "return bit32.rshift(2, 1)", "1"},
        {"bit32.countlz", "return bit32.countlz(1.5)", "invalid argument #1 to 'countlz' (number has no integer representation)", "return bit32.countlz(2)", "30"},
        {"bit32.countrz", "return bit32.countrz(1.5)", "invalid argument #1 to 'countrz' (number has no integer representation)", "return bit32.countrz(2)", "1"},
        {"bit32.byteswap", "return bit32.byteswap(1.5)", "invalid argument #1 to 'byteswap' (number has no integer representation)", "return bit32.byteswap(2)", "33554432"},

        -- string.byte, string.char, string.sub
        {"string.byte 2-arg", "local s = 'abc' return string.byte(s, 1.5)", "invalid argument #2 to 'byte' (number has no integer representation)", "local s = 'abc' return string.byte(s, 2)", "98"},
        {"string.byte 3-arg", "local s = 'abc' return string.byte(s, 1, 1.5)", "invalid argument #3 to 'byte' (number has no integer representation)", "local s = 'abc' return string.byte(s, 2, 2)", "98"},
        {"string.char arg 1", "return string.char(1.5)", "invalid argument #1 to 'char' (number has no integer representation)", "return string.char(2)", "\2"},
        {"string.char arg 2", "return string.char(65, 1.5)", "invalid argument #2 to 'char' (number has no integer representation)", "return string.char(65, 2)", "A\2"},
        {"string.sub arg 2", "local s = 'abcd' return string.sub(s, 1.5, 3)", "invalid argument #2 to 'sub' (number has no integer representation)", "local s = 'abcd' return string.sub(s, 2, 3)", "bc"},
        {"string.sub arg 3", "local s = 'abcd' return string.sub(s, 1, 1.5)", "invalid argument #3 to 'sub' (number has no integer representation)", "local s = 'abcd' return string.sub(s, 1, 2)", "ab"},

        -- buffer reads and writes
        {"buffer.readi8", "local b = buffer.create(16) return buffer.readi8(b, 1.5)", "invalid argument #2 to 'readi8' (number has no integer representation)", "local b = buffer.create(16) buffer.writei8(b, 2, -5) return buffer.readi8(b, 2)", "-5"},
        {"buffer.readu8", "local b = buffer.create(16) return buffer.readu8(b, 1.5)", "invalid argument #2 to 'readu8' (number has no integer representation)", "local b = buffer.create(16) buffer.writeu8(b, 2, 7) return buffer.readu8(b, 2)", "7"},
        {"buffer.writei8 offset", "local b = buffer.create(16) buffer.writei8(b, 1.5, 2) return 0", "invalid argument #2 to 'writei8' (number has no integer representation)", "local b = buffer.create(16) buffer.writei8(b, 2, 2) return buffer.readi8(b, 2)", "2"},
        {"buffer.writei8 value", "local b = buffer.create(16) buffer.writei8(b, 2, 1.5) return 0", "invalid argument #3 to 'writei8' (number has no integer representation)", "local b = buffer.create(16) buffer.writei8(b, 1, 2) return buffer.readi8(b, 1)", "2"},
        {"buffer.writeu8 offset", "local b = buffer.create(16) buffer.writeu8(b, 1.5, 2) return 0", "invalid argument #2 to 'writeu8' (number has no integer representation)", "local b = buffer.create(16) buffer.writeu8(b, 2, 2) return buffer.readu8(b, 2)", "2"},
        {"buffer.writeu8 value", "local b = buffer.create(16) buffer.writeu8(b, 2, 1.5) return 0", "invalid argument #3 to 'writeu8' (number has no integer representation)", "local b = buffer.create(16) buffer.writeu8(b, 1, 2) return buffer.readu8(b, 1)", "2"},
        {"buffer.readi16", "local b = buffer.create(16) return buffer.readi16(b, 1.5)", "invalid argument #2 to 'readi16' (number has no integer representation)", "local b = buffer.create(16) buffer.writei16(b, 2, -300) return buffer.readi16(b, 2)", "-300"},
        {"buffer.readu16", "local b = buffer.create(16) return buffer.readu16(b, 1.5)", "invalid argument #2 to 'readu16' (number has no integer representation)", "local b = buffer.create(16) buffer.writeu16(b, 2, 300) return buffer.readu16(b, 2)", "300"},
        {"buffer.writei16 offset", "local b = buffer.create(16) buffer.writei16(b, 1.5, 2) return 0", "invalid argument #2 to 'writei16' (number has no integer representation)", "local b = buffer.create(16) buffer.writei16(b, 2, 2) return buffer.readi16(b, 2)", "2"},
        {"buffer.writei16 value", "local b = buffer.create(16) buffer.writei16(b, 2, 1.5) return 0", "invalid argument #3 to 'writei16' (number has no integer representation)", "local b = buffer.create(16) buffer.writei16(b, 0, 2) return buffer.readi16(b, 0)", "2"},
        {"buffer.writeu16 offset", "local b = buffer.create(16) buffer.writeu16(b, 1.5, 2) return 0", "invalid argument #2 to 'writeu16' (number has no integer representation)", "local b = buffer.create(16) buffer.writeu16(b, 2, 2) return buffer.readu16(b, 2)", "2"},
        {"buffer.writeu16 value", "local b = buffer.create(16) buffer.writeu16(b, 2, 1.5) return 0", "invalid argument #3 to 'writeu16' (number has no integer representation)", "local b = buffer.create(16) buffer.writeu16(b, 0, 2) return buffer.readu16(b, 0)", "2"},
        {"buffer.readi32", "local b = buffer.create(16) return buffer.readi32(b, 1.5)", "invalid argument #2 to 'readi32' (number has no integer representation)", "local b = buffer.create(16) buffer.writei32(b, 2, -70000) return buffer.readi32(b, 2)", "-70000"},
        {"buffer.readu32", "local b = buffer.create(16) return buffer.readu32(b, 1.5)", "invalid argument #2 to 'readu32' (number has no integer representation)", "local b = buffer.create(16) buffer.writeu32(b, 2, 70000) return buffer.readu32(b, 2)", "70000"},
        {"buffer.writei32 offset", "local b = buffer.create(16) buffer.writei32(b, 1.5, 2) return 0", "invalid argument #2 to 'writei32' (number has no integer representation)", "local b = buffer.create(16) buffer.writei32(b, 2, 2) return buffer.readi32(b, 2)", "2"},
        {"buffer.writei32 value", "local b = buffer.create(16) buffer.writei32(b, 2, 1.5) return 0", "invalid argument #3 to 'writei32' (number has no integer representation)", "local b = buffer.create(16) buffer.writei32(b, 0, 2) return buffer.readi32(b, 0)", "2"},
        {"buffer.writeu32 offset", "local b = buffer.create(16) buffer.writeu32(b, 1.5, 2) return 0", "invalid argument #2 to 'writeu32' (number has no integer representation)", "local b = buffer.create(16) buffer.writeu32(b, 2, 2) return buffer.readu32(b, 2)", "2"},
        {"buffer.writeu32 value", "local b = buffer.create(16) buffer.writeu32(b, 2, 1.5) return 0", "invalid argument #3 to 'writeu32' (number has no integer representation)", "local b = buffer.create(16) buffer.writeu32(b, 0, 2) return buffer.readu32(b, 0)", "2"},
        {"buffer.readf32", "local b = buffer.create(16) return buffer.readf32(b, 1.5)", "invalid argument #2 to 'readf32' (number has no integer representation)", "local b = buffer.create(16) buffer.writef32(b, 2, 2) return buffer.readf32(b, 2)", "2"},
        {"buffer.writef32 offset", "local b = buffer.create(16) buffer.writef32(b, 1.5, 2) return 0", "invalid argument #2 to 'writef32' (number has no integer representation)", "local b = buffer.create(16) buffer.writef32(b, 2, 3.5) return buffer.readf32(b, 2)", "3.5"},
        {"buffer.readf64", "local b = buffer.create(16) return buffer.readf64(b, 1.5)", "invalid argument #2 to 'readf64' (number has no integer representation)", "local b = buffer.create(16) buffer.writef64(b, 2, 2) return buffer.readf64(b, 2)", "2"},
        {"buffer.writef64 offset", "local b = buffer.create(16) buffer.writef64(b, 1.5, 2) return 0", "invalid argument #2 to 'writef64' (number has no integer representation)", "local b = buffer.create(16) buffer.writef64(b, 2, 3.5) return buffer.readf64(b, 2)", "3.5"},

        -- math.ldexp, table.unpack / unpack, select
        {"math.ldexp", "return math.ldexp(3, 1.5)", "invalid argument #2 to 'ldexp' (number has no integer representation)", "return math.ldexp(3, 2)", "12"},
        {"table.unpack", "local t = {10, 20, 30} return select('#', table.unpack(t, 1, 1.5))", "invalid argument #3 to 'unpack' (number has no integer representation)", "local t = {10, 20, 30} return select('#', table.unpack(t, 1, 2))", "2"},
        {"unpack", "local t = {10, 20, 30} return select('#', unpack(t, 1, 1.5))", "invalid argument #3 to 'unpack' (number has no integer representation)", "local t = {10, 20, 30} return select('#', unpack(t, 1, 2))", "2"},
        {"select vararg", "local function f(...) local x = select(1.5, ...) return x end return f(10, 20, 30)", "invalid argument #1 to 'select' (number has no integer representation)", "local function f(...) local x = select(2, ...) return x end return f(10, 20, 30)", "20"},
    }

    local function runAt(src, level)
        local bytecode = moo_test.compile(src, level)
        local fn, loadErr = moo_test.load(bytecode, "=moo_fastcall")
        assert(fn, loadErr)
        return pcall(fn)
    end

    for _, tc in ipairs(cases) do
        local name, badCall, badMessage, okCall, okExpected = tc[1], tc[2], tc[3], tc[4], tc[5]

        local ok0, err0 = runAt(badCall, 0)
        local ok1, err1 = runAt(badCall, 1)
        local ok2, err2 = runAt(badCall, 2)
        assert(ok0 == false, name .. " (-O0): expected error, got " .. tostring(err0))
        assert(ok1 == false, name .. " (-O1): expected error, got " .. tostring(err1))
        assert(ok2 == false, name .. " (-O2): expected error, got " .. tostring(err2))
        assert(string.find(err0, badMessage, 1, true), name .. " (-O0): " .. tostring(err0))
        assert(err1 == err0, name .. " (-O1 vs -O0): " .. tostring(err1) .. " ~= " .. tostring(err0))
        assert(err2 == err0, name .. " (-O2 vs -O0): " .. tostring(err2) .. " ~= " .. tostring(err0))

        local rok0, res0 = runAt(okCall, 0)
        local rok1, res1 = runAt(okCall, 1)
        local rok2, res2 = runAt(okCall, 2)
        assert(rok0 == true, name .. " ok (-O0): " .. tostring(res0))
        assert(rok1 == true, name .. " ok (-O1): " .. tostring(res1))
        assert(rok2 == true, name .. " ok (-O2): " .. tostring(res2))
        assert(tostring(res0) == okExpected, name .. " ok (-O0): " .. tostring(res0) .. " ~= " .. okExpected)
        assert(tostring(res1) == tostring(res0), name .. " ok (-O1 vs -O0): " .. tostring(res1) .. " ~= " .. tostring(res0))
        assert(tostring(res2) == tostring(res0), name .. " ok (-O2 vs -O0): " .. tostring(res2) .. " ~= " .. tostring(res0))
    end
end

do
    -- moo: samples the tests/conformance/moo/spec layout. MooSpecScripts opens moo_test in a fresh VM and runs
    -- every file here in byte order; a file returns "OK" (docs/history/layer3/README.md).
    local value = moo_test.fromraw("98304")
    assert(value == 1.5, tostring(value))
    assert(moo_test.raw(value) == "98304", moo_test.raw(value))

    for level = 0, 2 do
        local bytecode = moo_test.compile("return 21 * 2", level)
        local fn, err = moo_test.load(bytecode, "=layout")
        assert(fn, err)
        assert(fn() == 42)
    end
end

do
    -- moo: DESIGN.md "Exact integers": a function that needs an integer accepts only an integer-valued
    -- number (its 16 fraction bits are zero); a fraction is the named error "number has no integer
    -- representation", as in Lua 5.3, instead of Luau's silent truncation. Every call uses a local alias
    -- so the bytecode cannot take a fastcall; the fastcall path is the first block of this file.
    local byte, char, sub, rep = string.byte, string.char, string.sub, string.rep
    local insert, remove, move, create = table.insert, table.remove, table.move, table.create
    local band, lshift = bit32.band, bit32.lshift
    local readu8, writeu8, readbits, writebits = buffer.readu8, buffer.writeu8, buffer.readbits, buffer.writebits
    local format, pack = string.format, string.pack

    local function refuses(call, message)
        local ok, err = pcall(call)
        assert(ok == false, message)
        assert(string.find(err, message, 1, true), err)
    end

    local b = buffer.create(8)
    local t = {10, 20, 30}

    refuses(function() return byte('abc', 1.5) end, "invalid argument #2 to 'byte' (number has no integer representation)")
    refuses(function() return char(1.5) end, "invalid argument #1 to 'char' (number has no integer representation)")
    refuses(function() return sub('abc', 1.5) end, "invalid argument #2 to 'sub' (number has no integer representation)")
    refuses(function() return rep('a', 1.5) end, "invalid argument #2 to 'rep' (number has no integer representation)")
    refuses(function() return insert(t, 1.5, 'x') end, "invalid argument #2 to 'insert' (number has no integer representation)")
    refuses(function() return remove(t, 1.5) end, "invalid argument #2 to 'remove' (number has no integer representation)")
    refuses(function() return move(t, 1.5, 2, 1, t) end, "invalid argument #2 to 'move' (number has no integer representation)")
    refuses(function() return create(1.5) end, "invalid argument #1 to 'create' (number has no integer representation)")
    refuses(function() return band(1.5) end, "invalid argument #1 to 'band' (number has no integer representation)")
    refuses(function() return lshift(1, 1.5) end, "invalid argument #2 to 'lshift' (number has no integer representation)")
    refuses(function() return readu8(b, 1.5) end, "invalid argument #2 to 'readu8' (number has no integer representation)")
    refuses(function() return readbits(b, 1.5, 1) end, "invalid argument #2 to 'readbits' (number has no integer representation)")
    for _, f in ipairs{'%d', '%i', '%x', '%X', '%c'} do
        refuses(function() return format(f, 1.5) end, "invalid argument #2 to 'format' (number has no integer representation)")
    end
    refuses(function() return pack('i4', 1.5) end, "invalid argument #2 to 'pack' (number has no integer representation)")
    refuses(function() return pack('I4', 1.5) end, "invalid argument #2 to 'pack' (number has no integer representation)")

    -- The same calls accept an integer-valued number, including one written 2.0.
    writeu8(b, 2.0, 7)
    assert(byte('abc', 2.0) == 98)
    assert(char(2.0) == '\2')
    assert(sub('abc', 2.0) == 'bc')
    assert(rep('a', 2.0) == 'aa')
    insert(t, 2.0, 15) assert(t[2] == 15 and #t == 4)
    assert(remove(t, 2.0) == 15 and #t == 3)
    assert(move(t, 2.0, 3, 1, t) == t)
    local c = create(2.0, 5) assert(#c == 2 and c[1] == 5)
    assert(band(2.0) == 2)
    assert(lshift(1, 2.0) == 4)
    assert(readu8(b, 2.0) == 7)
    writebits(b, 16.0, 3, 5) assert(readbits(b, 16.0, 3) == 5)
    assert(format('%d', 2.0) == '2')
    assert(format('%i', 2.0) == '2')
    assert(format('%x', 2.0) == '2')
    assert(format('%X', 2.0) == '2')
    assert(format('%c', 2.0) == '\2')
    assert(pack('i4', 2.0) == pack('i4', 2))
    assert(pack('I4', 2.0) == pack('I4', 2))
end

do
    -- moo: checked arithmetic (docs/DESIGN.md, "Overflow"). Release saturates; a checked build (MOO_OVERFLOW_ERRORS,
    -- on with assertions) raises "number overflow in '<op>'" instead, so a program either gives the release value or
    -- stops with the named error. This script runs in both builds: the first edge says which one this is, and every
    -- other operator must agree with it. tests/moo/number.test.cpp pins each build by name.
    local max = moo_test.fromraw("9223372036854775807") -- MOO_NUMBER_MAX, 2^47 - 2^-16
    local min = moo_test.fromraw("-9223372036854775807") -- MOO_NUMBER_MIN
    local step = moo_test.fromraw("1") -- 2^-16
    local big = moo_test.fromraw("4611686018427387904") -- 2^46

    local checked = not pcall(function(a, b) return a + b end, max, step)

    -- an operation past the edge: release saturates to want, a checked build raises the named error
    local function edge(name, want, fn, ...)
        local ok, result = pcall(fn, ...)
        if checked then
            assert(not ok, name .. " did not raise")
            local suffix = "number overflow in '" .. name .. "'"
            assert(string.sub(result, -#suffix) == suffix, result)
        else
            assert(ok, result)
            assert(result == want, name .. ": " .. moo_test.raw(result))
        end
    end

    -- one step inside the range: both builds give want
    local function inside(name, want, fn, ...)
        local ok, result = pcall(fn, ...)
        assert(ok, name .. ": " .. tostring(result))
        assert(result == want, name .. ": " .. moo_test.raw(result))
    end

    -- runtime operands (registers and constants), so nothing folds
    edge("+", max, function(a, b) return a + b end, max, step)
    edge("+", max, function(a) return a + 0.0000152587890625 end, max)
    edge("+", min, function(a, b) return a + b end, min, -step)
    inside("+", max, function(a, b) return a + b end, max - step, step)

    edge("-", min, function(a, b) return a - b end, min, step)
    edge("-", min, function(a) return a - 0.0000152587890625 end, min)
    edge("-", max, function(a) return 1 - a end, min)
    inside("-", max, function(a) return 1 - a end, min + 1)
    inside("-", min, function(a, b) return a - b end, min + step, step)

    edge("*", max, function(a, b) return a * b end, max, 1 + step)
    edge("*", max, function(a) return a * 2 end, big)
    edge("*", min, function(a) return a * -2 end, big)
    inside("*", max, function(a, b) return a * b end, max, 1)
    inside("*", max - step, function(a) return a * 2 end, big - step)

    edge("/", max, function(a, b) return a / b end, max, 1 - step)
    edge("/", max, function(a) return a / 0.5 end, big)
    edge("/", max, function(a) return 2147483648 / a end, step)
    edge("/", min, function(a, b) return a / b end, big, -0.5)
    inside("/", max, function(a, b) return a / b end, max, 1)
    inside("/", big, function(a) return 2147483648 / a end, step + step)

    edge("//", max, function(a, b) return a // b end, max, 1 - step)
    edge("//", max, function(a) return a // 0.5 end, big)
    edge("//", min, function(a, b) return a // b end, big, -0.5)
    inside("//", 140737488355327, function(a, b) return a // b end, max, 1)
    inside("//", 140737488355327, function(a) return a // 0.5 end, big - step)

    edge("^", max, function(a, b) return a ^ b end, 2, 47)
    edge("^", min, function(a) return a ^ 47 end, -2)
    edge("^", max, function(a) return a ^ 2.5 end, big)
    inside("^", 70368744177664, function(a) return a ^ 46 end, 2)

    -- the string coercion path (luaV_doarith) raises the same error
    edge("+", max, function(a, b) return a + b end, "140737488355327", max)

    -- unary - and % are exact on the domain and never saturate, in either build
    inside("unary -", min, function(a) return -a end, max)
    inside("unary -", max, function(a) return -a end, min)
    inside("%", max - step, function(a, b) return a % b end, max - step, max)
    inside("%", -step, function(a, b) return a % b end, max - step, -max)
    inside("%", 0, function(a, b) return a % b end, min, step)

    -- folded constants: a checked build keeps a saturating operation for run time, so it raises there too
    edge("+", max, function() return 140737488355327 + 1 end)
    edge("-", min, function() return -140737488355327 - 1 end)
    edge("*", max, function() return 70368744177664 * 2 end)
    edge("/", max, function() return 70368744177664 / 0.5 end)
    edge("//", max, function() return 70368744177664 // 0.5 end)
    edge("^", max, function() return 2 ^ 47 end)
    inside("+", 1073741825, function() return 1073741824 + 1 end)
    inside("*", 2147483648, function() return 1073741824 * 2 end)
end

return "OK"
