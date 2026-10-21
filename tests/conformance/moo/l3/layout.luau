-- moo: samples the tests/conformance/moo/l3 layout. MooL3Scripts opens moo_test in a fresh VM and runs
-- every file here in byte order; a file returns "OK" (docs/layer3/README.md).
local value = moo_test.fromraw("98304")
assert(value == 1.5, tostring(value))
assert(moo_test.raw(value) == "98304", moo_test.raw(value))

for level = 0, 2 do
    local bytecode = moo_test.compile("return 21 * 2", level)
    local fn, err = moo_test.load(bytecode, "=layout")
    assert(fn, err)
    assert(fn() == 42)
end

return "OK"
