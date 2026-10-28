-- This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
-- moo: expression-temporary reuse (docs/DESIGN.md "Expression-temporary reuse"; tests/moo/tempreuse.test.cpp).
-- The compiler marks an op whose operand is an expression temporary, and the VM may write that op's vec3, quat or
-- transform result into the temporary's object. It must never write into a value the program can see. Each block
-- below is one form a value can be visible in, compiled with reuse off (-O0) and on (-O1, -O2), and each asserts
-- that the visible value is unchanged and the result is right. MooSpecScripts runs this file and expects "OK".

local cases = [[
local a = vec3.new(1, 2, 3)
local b = vec3.new(10, 20, 30)
local s = 2

-- a local: the operand is the local's register, never a temporary
local l = a * s
local r = l + b
assert(l == vec3.new(2, 4, 6) and r == vec3.new(12, 24, 36))

-- an upvalue: read into a temporary by GETUPVAL, which is not an arithmetic producer
local u = a * s
local function addu(v) return u + v end
local ru = addu(b) - b
assert(u == vec3.new(2, 4, 6) and ru == vec3.new(2, 4, 6))

-- a table field: GETTABLEKS fills the temporary
local t = {f = a * s}
local rt = (t.f + b) - t.f
assert(t.f == vec3.new(2, 4, 6) and rt == b)

-- a call argument: the callee keeps its argument; the call result is not an arithmetic producer
local kept
local function keep(v) kept = v return v end
local rk = keep(a * s) + b
assert(kept == vec3.new(2, 4, 6) and rk == vec3.new(12, 24, 36))

-- a returned value: a function returns a product it also stores
local stored
local function make() local v = a * s stored = v return v end
local rm = make() + b
assert(stored == vec3.new(2, 4, 6) and rm == vec3.new(12, 24, 36))

-- a value captured by a closure in the same expression: the closure captures the local, never a temporary
local c = a * s
local rc = (c + b) + (function() return c end)()
assert(c == vec3.new(2, 4, 6) and rc == vec3.new(14, 28, 42))

-- the temporary handed to a metamethod, which keeps it and returns it: the next marked op must not write into it
local held
local mt = {__add = function(x, y) held = x return x end}
local obj = setmetatable({}, mt)
local rh = (a * s + obj) - b
assert(held == vec3.new(2, 4, 6) and rh == vec3.new(-8, -16, -24))

-- a metamethod that returns a value the program holds
local own = vec3.new(7, 8, 9)
local obj2 = setmetatable({}, {__mul = function() return own end})
local ro = (obj2 * 2) + b
assert(own == vec3.new(7, 8, 9) and ro == vec3.new(17, 28, 39))

-- a compound assignment: the variable gets the temporary's object; the old value of the variable is not written
local acc = vec3.new(0, 0, 0)
local before = acc
acc += a * s
acc += a * s
assert(before == vec3.new(0, 0, 0) and acc == vec3.new(4, 8, 12))

-- the same object twice in an expression never comes from one temporary: both operands are temporaries of their own
local d = (a * s) + (a * s)
assert(d == vec3.new(4, 8, 12) and a == vec3.new(1, 2, 3))

-- a table key: the key is a value of its own, unchanged by later arithmetic
local keys = {}
keys[a * s] = true
local rkey = (a * s) + b
assert(keys[vec3.new(2, 4, 6)] == true and rkey == vec3.new(12, 24, 36))

-- quat and transform temporaries
local q = quat.new(0, 0, 0.5, 0.5)
local qq = q * q
local rq = (q * q) * q
assert(qq == q * q and rq == qq * q)
local tf = transform.new(vec3.new(1, 0, 0))
local tt = tf * tf
local rtt = (tf * tf) * tf
assert(tt == transform.new(vec3.new(2, 0, 0)) and rtt == transform.new(vec3.new(3, 0, 0)))

-- numbers are never objects: marked ops compute numbers exactly as the unmarked ones do
local n = 3
assert((n * 2 + 1) * (n - 1) == 14 and -(n * n) == -9)

return "OK"
]]

for level = 0, 2 do
    local fn, err = moo_test.load(moo_test.compile(cases, level), "=tempreuse")
    assert(fn, err)
    local result = fn()
    assert(result == "OK", "level " .. level .. ": " .. tostring(result))
end

-- the compiler writes moo bytecode version 2, which adds the marked opcodes (Bytecode.h "moo bytecode versions")
assert(string.byte(moo_test.compile("return 1", 1), 1) == 0x82)

return "OK"
