-- moo: DESIGN.md "math library": math.noise is Luau's Perlin noise on fixed Q48.16.
local raw = moo_test.raw
local noise = math.noise

for x = -2, 2 do
    for y = -2, 2 do
        for z = -2, 2 do
            assert(noise(x, y, z) == 0, "noise is zero at lattice points")
        end
    end
end

local sample = noise(0.25, 0.5, 0.75)
assert(raw(math.noise(0.25, 0.5, 0.75)) == raw(noise(0.25, 0.5, 0.75)), "fastcall and library results match")
assert(sample >= -1 and sample <= 1, "noise stays in [-1, 1]")
assert(raw(noise(0.25, 0.5, 0.75)) == raw(noise(256.25, 0.5, 0.75)), "noise repeats every 256 cells")
assert(noise(0.25) == noise(0.25, 0, 0), "missing coordinates default to zero")
assert(math.noise(0.25) == noise(0.25), "fastcall supplies default coordinates")
assert(noise(0.25, 0.5) == noise(0.25, 0.5, 0), "missing z defaults to zero")
assert(noise(-0.25, -0.5, -0.75) == noise(255.75, 255.5, 255.25), "negative coordinates wrap by the period")

return "OK"
