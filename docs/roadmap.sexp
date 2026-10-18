; The moo roadmap: the data ROADMAP.md is generated from (nova-work roadmap verbs).
; Data, never evaluated. Edit with nova-work roadmap add/done/note, never ROADMAP.md by hand.
; Created 2026-10-18 when moo was carded into the sprint (stream moo, base sprint/moo).
(roadmap "v1"
 :title "moo roadmap"
 :text "The work left to build moo, one item per sprint card of stream moo, grouped by the layers of docs/DESIGN.md. Layers 1 and 2 and the first part of layer 3 are built (PRs #1-#15)."

 :groups
 (
  (group "layer-3" :title "Layer 3: number = Q48.16"
   :text "The rest of layer 3 (docs/DESIGN.md, Layers): the moo bytecode version, raw constants, the float boundary for buffers and string.pack, the float vector and integer types out, integer arguments, debug overflow errors, the patch inventory, and the bit-exact oracle against fixed's C.")
  (group "layer-3b" :title "Layer 3b: vec3, quat and transform"
   :text "Full Q48.16 vec3, quat and transform as immutable GC objects on fixed's functions, bit-exact against fixed3d.")
  (group "layer-4" :title "Layer 4: math on fixed"
   :text "The math library and pow on fixed with no libm, the CORE-MATH error table, and the formatting round-trip check.")
  (group "layer-5" :title "Layer 5: conformance"
   :text "The cross-platform determinism suite with adversarial programs, errors and tracebacks, Windows, fuzzing and the skip audit.")
  (group "layer-6" :title "Layer 6: first adoption"
   :text "Virtual Go's own determinism checks run against moo.")
 )

 :items
 (
  (item "moo-l3-test-layout" :group "layer-3" :area "moo"
   :title "Test layout: Moo.Test runs every tests/moo/*.test.cpp and tests/conformance/moo/l3/*.luau"
   :text "Sprint card moo-l3-test-layout (stream moo, tier pro, base sprint/moo)."
   :date "2026-10-18"
   :origin "sprint card moo-l3-test-layout, carded 2026-10-18")
  (item "moo-l3b-vec3-object" :group "layer-3b" :area "moo"
   :title "vec3 as an immutable GC object kind"
   :text "Sprint card moo-l3b-vec3-object (stream moo, tier heavy, base sprint/moo)."
   :date "2026-10-18"
   :origin "sprint card moo-l3b-vec3-object, carded 2026-10-18")
  (item "moo-l4-math-oracle" :group "layer-4" :area "moo"
   :title "the CORE-MATH accuracy oracle for every math function"
   :text "Sprint card moo-l4-math-oracle (stream moo, tier heavy, base sprint/moo)."
   :date "2026-10-18"
   :origin "sprint card moo-l4-math-oracle, carded 2026-10-18")
  (item "moo-l5-coroutine-programs" :group "layer-5" :area "moo"
   :title "adversarial coroutine programs from the model's states"
   :text "Sprint card moo-l5-coroutine-programs (stream moo, tier pro, base sprint/moo)."
   :date "2026-10-18"
   :origin "sprint card moo-l5-coroutine-programs, carded 2026-10-18")
  (item "moo-l6-virtualgo-nightly" :group "layer-6" :area "moo"
   :title "first adoption, Virtual Go's own determinism checks run nightly against moo"
   :text "Sprint card moo-l6-virtualgo-nightly (stream moo, tier pro, base sprint/moo)."
   :date "2026-10-18"
   :origin "sprint card moo-l6-virtualgo-nightly, carded 2026-10-18")
  (item "moo-l3-analyzer-builtins" :group "layer-3" :date "2026-10-18"
   :title "The analyzer's builtin definitions drop os, vector and integer"
   :text "Sprint card moo-l3-analyzer-builtins (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l3-analyzer-builtins, carded 2026-10-18")
  (item "moo-l3-ast-raw-literals" :group "layer-3" :date "2026-10-18"
   :title "Number literals reach the compiler as the exact raw value; the integer literal is refused"
   :text "Sprint card moo-l3-ast-raw-literals (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l3-ast-raw-literals, carded 2026-10-18")
  (item "moo-l3-boundary-tofloat" :group "layer-3" :date "2026-10-18"
   :title "The boundary to float: moo_tofloat, nearest float, ties to even, in integer arithmetic"
   :text "Sprint card moo-l3-boundary-tofloat (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l3-boundary-tofloat, carded 2026-10-18")
  (item "moo-l3-buffer-floats" :group "layer-3" :date "2026-10-18"
   :title "buffer.readf32/readf64/writef32/writef64 by the exact boundary rule"
   :text "Sprint card moo-l3-buffer-floats (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l3-buffer-floats, carded 2026-10-18")
  (item "moo-l3-bytecode-graph-fixed" :group "layer-3" :date "2026-10-18"
   :title "Sccp folds numbers with moo's operations, not doubles"
   :text "Sprint card moo-l3-bytecode-graph-fixed (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l3-bytecode-graph-fixed, carded 2026-10-18")
  (item "moo-l3-bytecode-version" :group "layer-3" :date "2026-10-18"
   :title "The moo bytecode version: number constants are the raw Q48.16 value"
   :text "Sprint card moo-l3-bytecode-version (stream moo, tier heavy, base sprint/moo)."
   :origin "sprint card moo-l3-bytecode-version, carded 2026-10-18")
  (item "moo-l3-compiler-raw-constants" :group "layer-3" :date "2026-10-18"
   :title "The compiler folds and emits raw Q48.16 constants, not doubles"
   :text "Sprint card moo-l3-compiler-raw-constants (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l3-compiler-raw-constants, carded 2026-10-18")
  (item "moo-l3-conformance" :group "layer-3" :date "2026-10-18"
   :title "Layer 3's closing gate: the oracle at scale, the bridges counted, the status written"
   :text "Sprint card moo-l3-conformance (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l3-conformance, carded 2026-10-18")
  (item "moo-l3-integer-args-fastcalls" :group "layer-3" :date "2026-10-18"
   :title "Fastcalls refuse a fraction the same way the library does"
   :text "Sprint card moo-l3-integer-args-fastcalls (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l3-integer-args-fastcalls, carded 2026-10-18")
  (item "moo-l3-integer-args" :group "layer-3" :date "2026-10-18"
   :title "Functions that need an integer refuse a fraction by name"
   :text "Sprint card moo-l3-integer-args (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l3-integer-args, carded 2026-10-18")
  (item "moo-l3-libs-out" :group "layer-3" :date "2026-10-18"
   :title "luaL_openlibs opens no vector and no integer library; the vector fastcalls are gone"
   :text "Sprint card moo-l3-libs-out (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l3-libs-out, carded 2026-10-18")
  (item "moo-l3-oracle-harness" :group "layer-3" :date "2026-10-18"
   :title "The oracle: every number operator bit-exact against fixed's C, random and edge operands"
   :text "Sprint card moo-l3-oracle-harness (stream moo, tier heavy, base sprint/moo)."
   :origin "sprint card moo-l3-oracle-harness, carded 2026-10-18")
  (item "moo-l3-overflow-errors-debug" :group "layer-3" :date "2026-10-18"
   :title "Debug builds raise number overflow in '+'; release saturates"
   :text "Sprint card moo-l3-overflow-errors-debug (stream moo, tier heavy, base sprint/moo)."
   :origin "sprint card moo-l3-overflow-errors-debug, carded 2026-10-18")
  (item "moo-l3-pack-floats" :group "layer-3" :date "2026-10-18"
   :title "string.pack/unpack f, d and n by the exact boundary rule"
   :text "Sprint card moo-l3-pack-floats (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l3-pack-floats, carded 2026-10-18")
  (item "moo-l3-patch-inventory" :group "layer-3" :date "2026-10-18"
   :title "docs/PATCHES.md and the CI check that every touched upstream file is in it"
   :text "Sprint card moo-l3-patch-inventory (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l3-patch-inventory, carded 2026-10-18")
  (item "moo-l3-readme-truth" :group "layer-3" :date "2026-10-18"
   :title "README: say what is built (layers 1 and 2, layer 3 in progress)"
   :text "Sprint card moo-l3-readme-truth (stream moo, tier flash, base sprint/moo)."
   :origin "sprint card moo-l3-readme-truth, carded 2026-10-18")
  (item "moo-l3-vector-api-out" :group "layer-3" :date "2026-10-18"
   :title "No vector in the C API"
   :text "Sprint card moo-l3-vector-api-out (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l3-vector-api-out, carded 2026-10-18")
  (item "moo-l3-vector-compiler-out" :group "layer-3" :date "2026-10-18"
   :title "The compiler knows no vector builtin and folds no vector"
   :text "Sprint card moo-l3-vector-compiler-out (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l3-vector-compiler-out, carded 2026-10-18")
  (item "moo-l3-vector-constant-refused" :group "layer-3" :date "2026-10-18"
   :title "moo bytecode has no vector constant: the builder never writes one, the loader refuses one by name"
   :text "Sprint card moo-l3-vector-constant-refused (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l3-vector-constant-refused, carded 2026-10-18")
  (item "moo-l3-vector-vm-out" :group "layer-3" :date "2026-10-18"
   :title "No vector arm in the VM; the last vector bridge calls go"
   :text "Sprint card moo-l3-vector-vm-out (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l3-vector-vm-out, carded 2026-10-18")
  (item "moo-l3b-analyzer-types" :group "layer-3b" :date "2026-10-18"
   :title "vec3, quat and transform as analyzer builtin types"
   :text "Sprint card moo-l3b-analyzer-types (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l3b-analyzer-types, carded 2026-10-18")
  (item "moo-l3b-conformance" :group "layer-3b" :date "2026-10-18"
   :title "Layer 3b's closing gate: bit-exact against fixed3d over dense inputs; notes folded"
   :text "Sprint card moo-l3b-conformance (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l3b-conformance, carded 2026-10-18")
  (item "moo-l3b-quat-object" :group "layer-3b" :date "2026-10-18"
   :title "quat as an immutable GC object kind (the vec3 pattern)"
   :text "Sprint card moo-l3b-quat-object (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l3b-quat-object, carded 2026-10-18")
  (item "moo-l3b-quat" :group "layer-3b" :date "2026-10-18"
   :title "quat operations (nlerp; slerp waits for fixed)"
   :text "Sprint card moo-l3b-quat (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l3b-quat, carded 2026-10-18")
  (item "moo-l3b-temp-reuse" :group "layer-3b" :date "2026-10-18"
   :title "expression-temporary reuse for vec3, quat and transform"
   :text "Sprint card moo-l3b-temp-reuse (stream moo, tier heavy, base sprint/moo)."
   :origin "sprint card moo-l3b-temp-reuse, carded 2026-10-18")
  (item "moo-l3b-transform-object" :group "layer-3b" :date "2026-10-18"
   :title "transform as an immutable GC object kind (the vec3 pattern)"
   :text "Sprint card moo-l3b-transform-object (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l3b-transform-object, carded 2026-10-18")
  (item "moo-l3b-transform" :group "layer-3b" :date "2026-10-18"
   :title "transform (fixed3d's b3Transform) operations"
   :text "Sprint card moo-l3b-transform (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l3b-transform, carded 2026-10-18")
  (item "moo-l3b-vec3-ops" :group "layer-3b" :date "2026-10-18"
   :title "vec3 operators and library on fixed's functions"
   :text "Sprint card moo-l3b-vec3-ops (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l3b-vec3-ops, carded 2026-10-18")
  (item "moo-l4-bridge-removed" :group "layer-4" :date "2026-10-18"
   :title "the double bridge is deleted"
   :text "Sprint card moo-l4-bridge-removed (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l4-bridge-removed, carded 2026-10-18")
  (item "moo-l4-conformance" :group "layer-4" :date "2026-10-18"
   :title "Layer 4's closing gate: the CORE-MATH error table, bit-exact against fixed, notes folded"
   :text "Sprint card moo-l4-conformance (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l4-conformance, carded 2026-10-18")
  (item "moo-l4-format-roundtrip" :group "layer-4" :date "2026-10-18"
   :title "Layer 4's formatting check: every formatted value round-trips"
   :text "Sprint card moo-l4-format-roundtrip (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l4-format-roundtrip, carded 2026-10-18")
  (item "moo-l4-math-exact-ops" :group "layer-4" :date "2026-10-18"
   :title "fmod, modf, frexp, ldexp, deg, rad and lerp as exact integer operations"
   :text "Sprint card moo-l4-math-exact-ops (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l4-math-exact-ops, carded 2026-10-18")
  (item "moo-l4-math-exp-log" :group "layer-4" :date "2026-10-18"
   :title "exp, log, log10, log(x, base), sinh, cosh and tanh on fixed"
   :text "Sprint card moo-l4-math-exp-log (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l4-math-exp-log, carded 2026-10-18")
  (item "moo-l4-math-inverse-trig" :group "layer-4" :date "2026-10-18"
   :title "asin, acos, atan and atan2 on fixed"
   :text "Sprint card moo-l4-math-inverse-trig (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l4-math-inverse-trig, carded 2026-10-18")
  (item "moo-l4-math-noise" :group "layer-4" :date "2026-10-18"
   :title "math.noise, Luau's Perlin noise, ported to fixed"
   :text "Sprint card moo-l4-math-noise (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l4-math-noise, carded 2026-10-18")
  (item "moo-l4-math-sqrt" :group "layer-4" :date "2026-10-18"
   :title "sqrt on fixSqrt; the hardware-sqrt seed question stays Glenn's"
   :text "Sprint card moo-l4-math-sqrt (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l4-math-sqrt, carded 2026-10-18")
  (item "moo-l4-math-trig" :group "layer-4" :date "2026-10-18"
   :title "sin, cos and tan on fixed"
   :text "Sprint card moo-l4-math-trig (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l4-math-trig, carded 2026-10-18")
  (item "moo-l4-pow-fractional" :group "layer-4" :date "2026-10-18"
   :title "^ with a non-integer exponent on fixPow; the bridge leaves pow"
   :text "Sprint card moo-l4-pow-fractional (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l4-pow-fractional, carded 2026-10-18")
  (item "moo-l4-pow-integer" :group "layer-4" :date "2026-10-18"
   :title "^ and math.pow with an integer exponent, exact and rounded once"
   :text "Sprint card moo-l4-pow-integer (stream moo, tier heavy, base sprint/moo)."
   :origin "sprint card moo-l4-pow-integer, carded 2026-10-18")
  (item "moo-l5-conformance" :group "layer-5" :date "2026-10-18"
   :title "Layer 5's closing gate: the cross-platform record, notes folded"
   :text "Sprint card moo-l5-conformance (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l5-conformance, carded 2026-10-18")
  (item "moo-l5-errors-tracebacks" :group "layer-5" :date "2026-10-18"
   :title "error messages and tracebacks compared byte for byte"
   :text "Sprint card moo-l5-errors-tracebacks (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l5-errors-tracebacks, carded 2026-10-18")
  (item "moo-l5-fuzz-differential" :group "layer-5" :date "2026-10-18"
   :title "a differential fuzz target, moo arithmetic and formatting against the oracle"
   :text "Sprint card moo-l5-fuzz-differential (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l5-fuzz-differential, carded 2026-10-18")
  (item "moo-l5-suite-audit" :group "layer-5" :date "2026-10-18"
   :title "every moved or skipped upstream test has its reason and its moo replacement"
   :text "Sprint card moo-l5-suite-audit (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l5-suite-audit, carded 2026-10-18")
  (item "moo-l5-windows-determinism" :group "layer-5" :date "2026-10-18"
   :title "the determinism suite on Windows MSVC and clang-cl, nightly"
   :text "Sprint card moo-l5-windows-determinism (stream moo, tier pro, base sprint/moo)."
   :origin "sprint card moo-l5-windows-determinism, carded 2026-10-18")
 )
)
