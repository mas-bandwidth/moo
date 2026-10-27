-- This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
-- moo: Errors: the messages and tracebacks of moo's runtime errors (docs/DESIGN.md "Division by zero").
-- Each block below is one rule; MooSpecScripts runs this file and expects "OK".

do
-- moo: layer 5 error messages and tracebacks (DETERMINISM.md 20). MooSpecScripts runs this in a
-- fresh VM with the moo_test helpers; it returns "OK" (docs/history/layer3/README.md). Each case pins the
-- exact error text of one kind the VM raises; tests/determinism/errors.luau pins the same strings
-- and their tracebacks byte for byte in the determinism suite.
local function checkThrows(f, wanted)
    local ok, e = pcall(f)
    assert(not ok, "expected an error, got success")
    assert(tostring(e):find(wanted, 1, true), "error did not contain " .. wanted .. ": " .. tostring(e))
end

checkThrows(function() return 1 / 0 end, "division by zero")
checkThrows(function() for i = 1, 10, 0 do end end, "'for' step is zero")
checkThrows(function() return 1 + nil end, "attempt to perform arithmetic (add) on number and nil")
checkThrows(function() local nv return nv() end, "attempt to call a nil value")
checkThrows(function() local t return t.x end, "attempt to index nil with 'x'")
checkThrows(function() local t = {} t[nil] = 1 end, "table index is nil")
checkThrows(function() return #setmetatable({}, {__len = function() return "x" end}) end, "'__len' must return a number")
checkThrows(function()
    local function rec(n) return rec(n + 1) end
    return rec(0)
end, "stack overflow")

do
    local ok, e = pcall(function()
        local meta = setmetatable({}, {__index = function() error("meta boom") end})
        return meta.x
    end)
    assert(not ok and tostring(e):find("meta boom", 1, true), "metamethod error did not propagate")
end
end

return "OK"
