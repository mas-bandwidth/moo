# moo-l4-math-noise

Decision: implement Luau Perlin noise directly in fixed Q48.16 with the same 256-entry hash permutation, gradient set, and left-to-right fade/lerp operation order. The period-256 reduction is exact in the raw fixed representation and keeps the table indices bounded. The 2^16 seeded-point test measured maximum absolute output difference from Luau's original float implementation of 0.000197619.

Changed:
- \`VM/src/lmathlib.cpp\`: \`perlin_fade\`, \`perlin_lerp\`, \`perlin_grad\`, \`moo_math_noise\`, and the \`math_noise\` library wrapper use \`moo_num*\` fixed operations. The library returns the shared fixed result without a double bridge.
- \`tests/moo/moo-l4-math-noise.test.cpp\`: frozen 64-point raw-output hash, lattice zeros, output range and 2^16 seeded comparison against a test-only copy of Luau's float reference.
- \`tests/conformance/moo/l3/moo-l4-math-noise.luau\`: lattice, range, 256-period, negative-coordinate and optional-argument behavior.

The frozen 64-point hash is \`12883597645287845634\` (FNV-1a over the eight little-endian bytes of each signed raw output). Every sampled output is in [-1, 1]; the 125 integer lattice points in [-2, 2]^3 return exactly zero.

Fastcall: appended LBF_MATH_NOISE after the existing enum values to preserve every current builtin ID, mapped it in the compiler, and routed the VM fastcall to the same moo_math_noise implementation as the library. The test compares direct fastcalls, local library aliases, and the shared core over 64 seeded points.

Exact DESIGN.md sentences for the layer closing card to update:
- \`| noise | Luau's Perlin noise, ported to fixed |\`
- \`Rebuilt on \`fixed\`. No libm.\`
- \`- \`VM/src/lmathlib.cpp:369\` \`math.noise\` (Perlin noise in doubles), \`:438\` \`math.lerp\`.\`

Exact DETERMINISM.md sentence for the layer closing card to update:
- \`**Fix:** floats leave the VM (layer 3) and the math library is rebuilt on \`fixed\` (layer 4): \`sin\`, \`cos\`, \`atan2\`, \`sqrt\`, \`exp2\`, \`log2\` from \`fixed\`, with the rest built from them in integer arithmetic. The compiler's folding calls the **same** fixed functions the VM calls, so folded and unfolded code agree bit for bit. See DESIGN.md, "math library".\`

Not done: no broad determinism audit or other layer 4 work was done.
