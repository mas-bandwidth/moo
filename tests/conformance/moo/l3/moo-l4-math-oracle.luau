-- moo: layer 4's accuracy oracle, seen from a script (docs/DESIGN.md, "math library", "Accuracy oracle"). The frozen
-- ideal answers are in tests/moo/vectors/math-oracle.bin, and the error table is printed by
-- tests/moo/moo-l4-math-oracle.test.cpp MooL4MathOracleErrorTable. When this card started every transcendental in math
-- was the libm bridge (docs/DESIGN.md, "Layer 3 as built"), so this script calls none of them: a test never calls libm. It
-- checks the two values the VM gives without libm that the file also holds. math.pi is the nearest Q48.16 to pi,
-- the file's ideal for acos(-1), and math.huge is the largest value, the file's saturated ideal (checked by
-- tests/moo/moo-l4-math-oracle.test.cpp MooL4MathOracleExactPoints).
assert(moo_test.raw(math.pi) == "205887")
assert(moo_test.raw(math.huge) == "9223372036854775807")
assert(moo_test.raw(-math.huge) == "-9223372036854775807")

return "OK"
