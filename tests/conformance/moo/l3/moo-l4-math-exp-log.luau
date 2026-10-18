-- moo: DESIGN.md "math library": exp/sinh/cosh/tanh use fixExp2; log/log10/log(x, base) use fixLog2
-- with the fixed scaling compositions. A direct call exercises the bytecode fastcall; an alias exercises the library.
local exp_lib = math.exp
local sinh_lib = math.sinh
local cosh_lib = math.cosh
local tanh_lib = math.tanh
local log_lib = math.log
local log_base_lib = math.log
local log10_lib = math.log10

assert(math.exp(0) == 1)
assert(math.exp(100) == math.huge)
assert(math.exp(-100) == 0)
assert(math.sinh(0) == 0)
assert(math.cosh(0) == 1)
assert(math.tanh(0) == 0)
assert(math.sinh(100) == math.huge)
assert(math.sinh(-100) == -math.huge)
assert(math.cosh(100) == math.huge)
assert(math.tanh(100) == 1)
assert(math.tanh(-100) == -1)
assert(math.log(1) == 0)
assert(math.log10(1) == 0)
assert(math.log(8, 2) == 3)

for _, x in ipairs({-40, -1, 0, 1, 10, 40}) do
    assert(exp_lib(x) == math.exp(x), "exp fastcall vs library")
    assert(sinh_lib(x) == math.sinh(x), "sinh fastcall vs library")
    assert(cosh_lib(x) == math.cosh(x), "cosh fastcall vs library")
    assert(tanh_lib(x) == math.tanh(x), "tanh fastcall vs library")
end

for _, x in ipairs({1, 2, 8, 10, 100}) do
    assert(log_lib(x) == math.log(x), "log fastcall vs library")
    assert(log10_lib(x) == math.log10(x), "log10 fastcall vs library")
    assert(log_base_lib(x, 2) == math.log(x, 2), "log(x, 2) fastcall vs library")
    assert(log_base_lib(x, 3) == math.log(x, 3), "log(x, 3) fastcall vs library")
end

local function domainError(call)
    local ok, err = pcall(call)
    return not ok and string.find(err, "math.log of a non-positive number", 1, true) ~= nil
end
assert(domainError(function() return math.log(0) end))
assert(domainError(function() return log_lib(-1) end))
assert(domainError(function() return math.log(1, 0) end))
assert(domainError(function() return math.log(1, -1) end))
assert(domainError(function() return math.log10(0) end))
assert(domainError(function() return log10_lib(-1) end))

local ok, err = pcall(function() return math.log(1, 1) end)
assert(not ok and string.find(err, "division by zero", 1, true) ~= nil, err)
ok, err = pcall(function() return log_lib(1, 1) end)
assert(not ok and string.find(err, "division by zero", 1, true) ~= nil, err)

return "OK"
