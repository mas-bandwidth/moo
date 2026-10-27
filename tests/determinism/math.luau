-- moo: layer 4's determinism program (docs/DETERMINISM.md, 7). It exercises every math function and the
-- ^ operator on adversarial inputs -- the domain edges, the small fractions whose literals are not exact
-- binaries, large magnitudes, and the named-error cases -- and logs every result with tostring, so the
-- effects and state hashes pin the fixed-point library across platforms and compilers.

local values = {
    -100, -10, -3.5, -2, -1.5, -1, -0.5, -0.1, -0.0000152587890625,
    0, 0.0000152587890625, 0.1, 0.5, 1, 1.5, 2, 3.5, 10, 100, 1000,
    65536, 2147483647, -2147483648,
}

local function emit(name, f, x)
    local ok, a, b = pcall(f, x)
    log(name, ok and tostring(a) or a, b ~= nil and tostring(b) or "")
end

for _, x in ipairs(values) do
    emit("abs", math.abs, x)
    emit("ceil", math.ceil, x)
    emit("floor", math.floor, x)
    emit("round", math.round, x)
    emit("sign", math.sign, x)
    emit("sqrt", math.sqrt, x)
    emit("deg", math.deg, x)
    emit("rad", math.rad, x)
    emit("sin", math.sin, x)
    emit("cos", math.cos, x)
    emit("tan", math.tan, x)
    emit("asin", math.asin, x)
    emit("acos", math.acos, x)
    emit("atan", math.atan, x)
    emit("exp", math.exp, x)
    emit("log", math.log, x)
    emit("log10", math.log10, x)
    emit("sinh", math.sinh, x)
    emit("cosh", math.cosh, x)
    emit("tanh", math.tanh, x)
    emit("fmod", function(v) return math.fmod(v, 3) end, x)
    emit("modf", math.modf, x)
    emit("frexp", math.frexp, x)
    emit("ldexp", function(v) return math.ldexp(v, 3) end, x)
end

-- a ^ b as text, or its error. A power above 2^46 saturates in release and raises in a checked build, which
-- would give the two builds different hashes, so it is logged as "large" and not computed.
local function pow(a, b)
    if a ~= 0 and math.log(math.abs(a), 2) * b > 46 then
        return "large"
    end
    local ok, v = pcall(function() return a ^ b end)
    return ok and tostring(v) or v
end

for __, a in ipairs(values) do
    for ___, b in ipairs(values) do
        if b ~= 0 then
            log("atan2", tostring(math.atan2(a, b)))
            log("pow", pow(a, b))
        end
    end
end

-- exact integer-valued exponentiation and the saturated edges
for _, e in ipairs{-5, -2, -1, 0, 1, 2, 3, 5, 17, 63} do
    log("pow2", tostring(e), pow(2, e))
end
log("pow0", pow(0, -1), pow(-4, 0.5))
log("huge", tostring(math.huge), tostring(-math.huge))
log("pi", tostring(math.pi), tostring(math.e), tostring(math.tau), tostring(math.phi), tostring(math.sqrt2))

-- named errors
for _, c in ipairs{
    {"sqrt", function() return math.sqrt(-1) end},
    {"log0", function() return math.log(0) end},
    {"logbase", function() return math.log(10, 1) end},
    {"asin", function() return math.asin(1.5) end},
    {"acos", function() return math.acos(-1.5) end},
    {"fmod", function() return math.fmod(1, 0) end},
    {"tan", function() return math.tan(1.5707963267948966) end},
} do
    local ok, err = pcall(c[2])
    log(c[1], tostring(ok), tostring(err))
end

-- clamp / min / max / lerp / map / noise / random
for _, x in ipairs(values) do
    log("clamp", tostring(math.clamp(x, -2, 2)))
    log("minmax", tostring(math.min(x, 0.5)), tostring(math.max(x, 0.5)))
    log("lerp", tostring(math.lerp(x, 2, 0.25)), tostring(math.lerp(x, 2, 1)))
    log("map", tostring(math.map(x, -10, 10, -1, 1)))
end
log("noise", tostring(math.noise(0.5, 1.5, 2.5)), tostring(math.noise(3.25)))
for i = 1, 8 do
    log("random", tostring(math.random()), tostring(math.random(1, 100)), tostring(math.random(1000)))
end

return "OK"
