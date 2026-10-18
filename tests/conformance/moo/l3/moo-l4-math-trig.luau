-- moo: DESIGN.md "math library": math.sin, math.cos and math.tan on fixed. sin and cos come from
-- fixComputeCosSin (fixSin/fixCos) and tan is sin/cos with a zero cosine raising "division by zero" --
-- the library and the fastcall share one definition, so they give the same bits. The direct calls below
-- take the bytecode fastcall; the local aliases go through the library.
local f_sin = math.sin
local f_cos = math.cos
local f_tan = math.tan

local pi = moo_test.fromraw('205887')     -- round(pi * 2^16)
local halfpi = moo_test.fromraw('102944') -- round(pi/2 * 2^16)

-- sin/cos at the special angles agree between the fastcall and the library path.
for _, a in ipairs{0, halfpi, 2 * halfpi, 3 * halfpi, -halfpi, -2 * halfpi, -3 * halfpi} do
    assert(f_sin(a) == math.sin(a), "sin fastcall vs library")
    assert(f_cos(a) == math.cos(a), "cos fastcall vs library")
end

-- sin(0) = 0, cos(0) = 1, sin(pi/2) = 1, cos(pi/2) = 0, sin(pi) = 0, cos(pi) = -1.
assert(math.sin(0) == 0)
assert(math.cos(0) == 1)
assert(math.sin(halfpi) == 1)
assert(math.cos(halfpi) == 0)
assert(math.sin(pi) == 0)
assert(math.cos(pi) == -1)

-- tan = sin / cos; a zero cosine is "division by zero", on the fastcall and the library path.
local ok, err = pcall(math.tan, halfpi)
assert(ok == false and err == "division by zero", err)
ok, err = pcall(f_tan, -halfpi)
assert(ok == false and err == "division by zero", err)
ok, err = pcall(f_tan, halfpi)
assert(ok == false and err == "division by zero", err)

-- tan and its fastcall agree where the cosine is not zero.
for _, a in ipairs{0, pi, 2 * pi, -pi, 1} do
    assert(f_tan(a) == math.tan(a), "tan fastcall vs library")
end
assert(math.tan(0) == 0)
assert(math.tan(pi) == 0)

return "OK"
