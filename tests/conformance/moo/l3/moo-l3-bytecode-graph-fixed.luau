-- moo: the bytecode-graph constant propagation (Sccp) folds with moo_number.h, so a folded constant is the VM's
-- result (docs/layer3/moo-l3-bytecode-graph-fixed.md; docs/DESIGN.md "Layer 3 as built"). The raw Q48.16 values
-- below are what the VM computes; the compiler may fold them into constants, but the observable value is the same.
local function check(expr, want)
    local got = expr()
    assert(moo_test.raw(got) == want, moo_test.raw(got) .. " want " .. want)
end

check(function() return 1.5 + 2.25 end, "245760")   -- 3.75 * 65536
check(function() return 10 / 4 end, "163840")       -- 2.5 * 65536
check(function() return 7 // 2 end, "196608")       -- 3 * 65536
check(function() return 7 % 3 end, "65536")         -- 1 * 65536

-- division by zero (and % by zero) are never folded: the VM raises the named error at run time
local ok, err = pcall(function() return 10 / 0 end)
assert(not ok, "10/0 should error")
assert(err and err:find("division by zero") ~= nil, tostring(err))

local ok2, err2 = pcall(function() return 10 % 0 end)
assert(not ok2, "10%0 should error")
assert(err2 and err2:find("division by zero") ~= nil, tostring(err2))

return "OK"
