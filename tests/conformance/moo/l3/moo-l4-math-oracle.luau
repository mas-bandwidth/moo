-- moo: layer 4's accuracy oracle, through the VM (docs/DESIGN.md, "math library", "Accuracy oracle"). The frozen
-- ideal answers are in tests/moo/vectors/math-oracle.bin and the error table is printed by
-- tests/moo/moo-l4-math-oracle.test.cpp MooL4MathOracleErrorTable. This file checks, through the VM, the points
-- where the real answer is exact; tests/moo/moo-l4-math-oracle.test.cpp MooL4MathOracleExactPoints checks that the
-- vector file holds the same ideal values there.
local function raw(x)
    return moo_test.raw(x)
end

assert(raw(math.sin(0)) == "0")
assert(raw(math.cos(0)) == "65536")
assert(raw(math.tan(0)) == "0")
assert(raw(math.asin(0)) == "0")
assert(raw(math.acos(1)) == "0")
assert(raw(math.atan(0)) == "0")
assert(raw(math.atan2(0, 1)) == "0")
assert(raw(math.exp(0)) == "65536")
assert(raw(math.log(1)) == "0")
assert(raw(math.log10(1)) == "0")
assert(raw(math.log(1, 2)) == "0")
assert(raw(math.sinh(0)) == "0")
assert(raw(math.cosh(0)) == "65536")
assert(raw(math.tanh(0)) == "0")
assert(raw(math.sqrt(1)) == "65536")
assert(raw(math.sqrt(0)) == "0")
assert(raw(math.pow(1, 1)) == "65536")
assert(raw(1 ^ 1) == "65536")

return "OK"
