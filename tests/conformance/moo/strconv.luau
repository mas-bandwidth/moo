-- This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
-- This file is based on Lua 5.x tests -- https://github.com/lua/lua/tree/master/testes
print("testing string-number conversion")

-- zero
assert(tostring(0) == "0")
-- moo: no negative zero: assert(tostring(0/-1) == "-0")

-- specials
-- moo: no infinity or NaN: assert(tostring(1/0) == "inf")
-- moo: no infinity or NaN: assert(tostring(-1/0) == "-inf")
-- moo: no infinity or NaN: assert(tostring(0/0) == "nan")

-- integers
assert(tostring(1) == "1")
assert(tostring(42) == "42")
assert(tostring(-4294967296) == "-4294967296")
-- moo: outside the range (about +-1.4e14) or below the resolution 2^-16: assert(tostring(9007199254740991) == "9007199254740991")

-- decimals
assert(tostring(0.5) == "0.5")
assert(tostring(0.1) == "0.1")
assert(tostring(-0.17) == "-0.17")
-- moo: tostring prints the shortest decimal that reads back to the same 2^-16 value: assert(tostring(math.pi) == "3.141592653589793")

-- scientific
-- moo: outside the range (about +-1.4e14) or below the resolution 2^-16: assert(tostring(1e+30) == "1e+30")
-- moo: outside the range (about +-1.4e14) or below the resolution 2^-16: assert(tostring(-1e+24) == "-1e+24")

-- fuzzing corpus
-- Note: If the assert below fires it may indicate floating point denormalized values
-- are not handled as expected.
-- moo: outside the range (about +-1.4e14) or below the resolution 2^-16: assert(tostring(5.4536123983019448e-311) == "5.453612398302e-311")
-- moo: outside the range (about +-1.4e14) or below the resolution 2^-16: assert(tostring(5.4834368411298348e-311) == "5.48343684113e-311")
-- moo: outside the range (about +-1.4e14) or below the resolution 2^-16: assert(tostring(4.4154895841930002e-305) == "4.415489584193e-305")
-- moo: outside the range (about +-1.4e14) or below the resolution 2^-16: assert(tostring(1125968630513728) == "1125968630513728")
-- moo: outside the range (about +-1.4e14) or below the resolution 2^-16: assert(tostring(3.3951932655938423e-313) == "3.3951932656e-313")
assert(tostring(1.625) == "1.625")
-- moo: outside the range (about +-1.4e14) or below the resolution 2^-16: assert(tostring(4.9406564584124654e-324) == "5e-324")
-- moo: tostring prints the shortest decimal that reads back to the same 2^-16 value: assert(tostring(2.0049288280105384) == "2.0049288280105384")
-- moo: tostring prints the shortest decimal that reads back to the same 2^-16 value: assert(tostring(3.0517578125e-05) == "0.000030517578125")
-- moo: tostring prints the shortest decimal that reads back to the same 2^-16 value: assert(tostring(1.383544921875) == "1.383544921875")
-- moo: tostring prints the shortest decimal that reads back to the same 2^-16 value: assert(tostring(3.0053350932691001) == "3.0053350932691")
-- moo: tostring prints the shortest decimal that reads back to the same 2^-16 value: assert(tostring(0.0001373291015625) == "0.0001373291015625")
-- moo: outside the range (about +-1.4e14) or below the resolution 2^-16: assert(tostring(-1.9490628022799998e+289) == "-1.94906280228e+289")
-- moo: tostring prints the shortest decimal that reads back to the same 2^-16 value: assert(tostring(-0.00610404721867928) == "-0.00610404721867928")
-- moo: tostring prints the shortest decimal that reads back to the same 2^-16 value: assert(tostring(0.00014495849609375) == "0.00014495849609375")
-- moo: tostring prints the shortest decimal that reads back to the same 2^-16 value: assert(tostring(0.453125) == "0.453125")
-- moo: outside the range (about +-1.4e14) or below the resolution 2^-16: assert(tostring(-4.2375343999999997e+73) == "-4.2375344e+73")
-- moo: outside the range (about +-1.4e14) or below the resolution 2^-16: assert(tostring(1.3202313930270133e-192) == "1.3202313930270133e-192")
-- moo: outside the range (about +-1.4e14) or below the resolution 2^-16: assert(tostring(3.6984408976312836e+19) == "36984408976312840000")
-- moo: tostring prints the shortest decimal that reads back to the same 2^-16 value: assert(tostring(2.0563000527063302) == "2.05630005270633")
-- moo: outside the range (about +-1.4e14) or below the resolution 2^-16: assert(tostring(4.8970527433648997e-260) == "4.8970527433649e-260")
-- moo: tostring prints the shortest decimal that reads back to the same 2^-16 value: assert(tostring(1.62890625) == "1.62890625")
-- moo: outside the range (about +-1.4e14) or below the resolution 2^-16: assert(tostring(1.1295093211933533e+65) == "1.1295093211933533e+65")

return "OK"

-- moo-specific expected behaviour: upstream tests/conformance/strconv.luau with moo's numbers.
-- Why: numbers are Q48.16 fixed point (docs/DESIGN.md): there is no infinity, NaN or negative zero, division by zero is a named error, and the range is about +-1.4e14.
-- Every changed line is marked "-- moo:" with its reason; the rest is upstream, unchanged.
