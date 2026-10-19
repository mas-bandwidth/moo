-- moo: these checks keep the VM opcode families working after its unreachable vector arms are removed.
local number = 9
local quotient = number / 3
local sum = number + quotient
assert(sum == 12 and number - quotient == 6 and number * quotient == 9)
assert(number // 2 == 4 and number % 2 == 1 and -number == -9)

local left = setmetatable({ value = 4 }, { __add = function(a, b) return a.value + b end })
assert(left + 3 == 7)
local tableValue = { key = "value", [2] = 8 }
assert(tableValue.key == "value" and tableValue[2] == 8)
assert(("moo"):sub(2, 2) == "o")

local function identify(value)
    return value
end
assert(identify(number) == 9)
assert(identify(tableValue) == tableValue)
assert(identify("moo") == "moo")

assert(number == 9 and number < 10)
assert(tableValue == tableValue and tableValue ~= {})
assert("moo" == "moo" and "moo" < "moon")

return "OK"
