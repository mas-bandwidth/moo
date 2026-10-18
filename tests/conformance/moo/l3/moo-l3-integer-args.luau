-- moo: DESIGN.md "Exact integers": a function that needs an integer accepts only an integer-valued
-- number (its 16 fraction bits are zero); a fraction is the named error "number has no integer
-- representation", as in Lua 5.3, instead of Luau's silent truncation. Every call uses a local alias
-- so the bytecode cannot take a fastcall; the fastcall path is card moo-l3-integer-args-fastcalls.
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

return "OK"
