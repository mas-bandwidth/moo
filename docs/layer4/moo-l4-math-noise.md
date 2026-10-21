# moo-l4-math-noise

Decision: implement Luau Perlin noise directly in fixed Q48.16 with the same 256-entry hash permutation, gradient set, and left-to-right fade/lerp operation order. The period-256 reduction is exact in the raw fixed representation and keeps the table indices bounded. The 2^16 seeded-point test measured maximum absolute output difference from Luau's original float implementation of 0.000197619.

Changed:
- \`VM/src/lmathlib.cpp\`: \`perlin_fade\`, \`perlin_lerp\`, \`perlin_grad\`, \`moo_math_noise\`, and the \`math_noise\` library wrapper use \`moo_num*\` fixed operations. The library returns the shared fixed result without a double bridge.
- \`VM/src/lbuiltins.cpp\`: \`luauF_noise\` calls the same \`moo_math_noise\`; the fastcall table slot is appended before the compatibility dummies.
- \`Compiler/src/Builtins.cpp\` and \`Common/include/Luau/Bytecode.h\`: \`math.noise\` maps to the new appended \`LBF_MATH_NOISE\`, so every existing builtin id is preserved.
- \`tests/Compiler.test.cpp\`: the existing \`JumpFold\` snapshot now expects the \`math.noise\` FASTCALL (and the constant indices it shifts); this is the only upstream expectation the fastcall changes.
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

Pre-existing gate red at BASE 3234bcbd27bd9f04b626e674d7fe5058b31488dc (not caused by this card; the same cases and assertion counts fail on the unchanged base, built and run beside this branch):

- \`Moo.Test\`: 4 failed test cases at both BASE and this head, 44 failed assertions at both: \`MooL3Scripts\` (the landed \`moo-l4-pow-integer-math.luau\` expects fractional exponents on the libm bridge; \`moo-l4-pow-fractional\` now answers them with \`fixPow\`), \`MathPowFractionalStaysOnBridge\`, \`MooPowIntVmFractionalBridge\`, \`MooL5SkippedCasesNameAReason\` (42 doctest skips where it expects 29). The three new moo-l4-math-noise cases pass (384 assertions).
- \`Luau.UnitTest\`: 13 failed test cases at both BASE and this head, 14 failed assertions at both, identical names: \`builtin_table_names\`, \`compound_assign_reports_invalid_vector_arithmetic\`, \`fold_constants_div_by_zero\`, \`issubtypeof\`, \`MooBuiltinsDoNotFold\`, \`persistent_sealed_table_is_immutable\`, \`type_narrow_to_vector\`, \`type_tostring\`, \`type_vector_refine\`, \`udtf_cloner_missing_integer_crashes_copy\`, \`udtf_integer_constructor_is_not_number\`, \`udtf_integer_is_distinct_from_number\`, \`udtf_integer_methods_work\`. Before the \`tests/Compiler.test.cpp\` snapshot update this head had one more (\`JumpFold\`, the fastcall change); the update removes it.

Not done: the pre-existing \`Moo.Test\` and \`Luau.UnitTest\` red above is left for its owning cards; no broad determinism audit or other layer 4 work was done.

Attempt 5 reverification (rebuilt on sprint/moo tip fd63990683f9b175f0bbb6f9fdc256024308ee03, clang 18, RelWithDebInfo, -DLUAU_WERROR=ON): the DONE-WHEN gate passes and the failing set at this head is identical to the documented BASE red, byte for byte. \`Moo.Test\`: 4 failed cases / 44 failed assertions (\`MooL3Scripts\` on \`moo-l4-pow-integer-math.luau\` only, \`MathPowFractionalStaysOnBridge\`, \`MooPowIntVmFractionalBridge\`, \`MooL5SkippedCasesNameAReason\`); the three \`MathNoise*\` cases pass, \`MathNoiseMaximumDifferenceFromLuauFloat=0.000197619\`. \`Luau.UnitTest\`: 13 failed cases / 14 failed assertions, the same 13 names as BASE. \`Luau.Conformance\` 109/109, \`Luau.CLI.Test\` 113/113 pass. \`tools/moo-check-no-clock.sh build/libLuau.VM.a\` clean; \`grep -c 'float'\` over lmathlib.cpp:489-559 prints 0; \`tools/revendor-fixed.sh --check\` clean.
