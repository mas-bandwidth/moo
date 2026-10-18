-- moo: the bytecode constant for a vector is refused by name (docs/layer3/moo-l3-vector-constant-refused.md; docs/DESIGN.md
-- "vec3, quat and transform"). The builder never writes LBC_CONSTANT_VECTOR (7) or LBC_CONSTANT_VECTORD (11), and the VM
-- refuses a hand-built moo chunk holding either tag (Common/include/Luau/Bytecode.h "Types of constant table entries"), before
-- it reads the payload.

-- The body of a hand-built chunk in Luau version 9 format (LBC_VERSION_TARGET at BASE), types version 3: no strings, no
-- userdata types, one proto with no instructions (the constant is read before any instruction runs), holding `constant`.
local function chunkWithConstant(constant)
    local s = string.char(0x81, 9, 3, 0, 0, 1) -- moo version 1, Luau version, types version, strings, userdata, protos
    s = s .. string.char(1, 0, 0, 0, 0, 0)     -- maxstacksize, numparams, nups, is_vararg, flags, type info size
    s = s .. string.char(0)                     -- sizecode: no instructions
    s = s .. string.char(1)                     -- sizek
    s = s .. constant
    s = s .. string.char(0)                     -- sizep
    s = s .. string.char(0, 0, 0, 0)            -- linedefined, debugname, lineinfo, debuginfo
    s = s .. string.char(0)                     -- main proto
    return s
end

-- LBC_CONSTANT_VECTOR (7) followed by sixteen bytes of float32 payload; the refusal fires before they are read
local fn, err = moo_test.load(chunkWithConstant(string.char(7) .. string.rep(string.char(0), 16)), "=vc")
assert(fn == nil)
assert(err == "vc: moo bytecode has no vector constant", err)

-- LBC_CONSTANT_VECTORD (11) followed by thirty-two bytes of float64 payload; the refusal fires before they are read
local dnf, derr = moo_test.load(chunkWithConstant(string.char(11) .. string.rep(string.char(0), 32)), "=vcd")
assert(dnf == nil)
assert(derr == "vcd: moo bytecode has no vector constant", derr)

return "OK"
