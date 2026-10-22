# moo-v2-l3b-conformance

Layer 3b's closing gate, items (1), (2) and (4) (`docs/DESIGN.md` "Conformance (layer 3b)"). Roadmap row "Bit-exact
against fixed and fixed3d; layer 3b closing gate", card `moo-v2-l3b-conformance-oracle`. Stacked on the analyzer PR
(#36).

## (1) The 3b oracle in Moo.Test

`tests/moo/moo-v2-l3b-conformance.test.cpp`, doctest suite `moo-v2-l3b-conformance`, case `Layer3bOracle`. All 22
operations of the three types run in moo through script functions called through the C API (nothing folds), and in C
on the same operands:

| operations | C oracle | inputs |
|---|---|---|
| `vec3 + - *`, `vec3 * number`, `number * vec3`, `-vec3` | the layer 3 oracle's independent definitions (`tests/moo/moo_oracle_fixed.c`, `moo_oracle_add/sub/mul/unm`), reused by name, per component | full range |
| `vec3 == vec3`, `quat == quat`, `transform == transform` | component equality | full range, equal, one raw unit apart |
| `vec3.dot`, `length`, `normalize` | `fixDot`, `fixLength`, `fixNormalize`; dot also against an exact dot product written in the test | full range |
| `vec3.cross` | `fixCross` | in range (2^40 raw) |
| `quat * quat`, `quat.conjugate` | `fixMulQuat`, `fixConjugate` | full range and rotations |
| `quat * vec3`, `quat.nlerp` | `fixRotateVector`, `fixNLerp` | rotations, in-range vectors, alpha 0, 1/2, 1 and random |
| `transform * transform`, `* vec3`/`point`, `invpoint` | fixed's rotation and the layer 3 oracle's saturating add; where fixed's plain add does not overflow, also `fixMulTransforms`, `fixTransformPoint`, `fixInvTransformPoint` | rigid in range, and identity rotations with full-range positions |
| `transform.direction`, `invdirection`, `inverse` | `fixRotateVector`, `fixInvRotateVector`, `fixInvertTransform` | rigid in range (and full-range translations for inverse) |

Input shapes: random magnitudes and signs over the full range with the edges (0, +-1 raw, +-1, 1/2, +-2^47 raw, the
domain ends and their neighbours) one time in four; zero, axis-aligned and two-equal-component vectors; unit
quaternions through `fixNormalizeQuat`, near-unit ones (each component a few raw units off), the identity and the exact
half turns. The count is 2^14 per operation (`MOO_ORACLE_3B_N` sets it); seeds are fixed per operation.

Summary line, studio (AppleClang arm64, RelWithDebInfo):

    moo-3b-oracle ops=22 cases=360448 per-op=16384 mismatches=0 transform-also-fixed=48170 transform-saturated=982 fixdot-wraps=0
    MOO_ORACLE_3B_N=4194304:
    moo-3b-oracle ops=22 cases=92274688 per-op=4194304 mismatches=0 transform-also-fixed=12326368 transform-saturated=256544 fixdot-wraps=131

(the second run took 11.6 s). The case asserts no mismatch, 22 operations, that both transform regimes are reached,
and that `fixDot` differs from the exact dot product only where its 128-bit sum wraps.

Planted error, run once and reverted: `transform.point` with fixed's plain `fixVecAdd` instead of the saturating add
turned the oracle red on the saturation cases (moo `9223372036838767077` where the oracle has `-9223372036854775807`).

## A finding: fixDot wraps at the top of the range (upstream, fixed)

`fixDotRaw` sums three 126-bit products in 128 bits through unsigned arithmetic. A sum of magnitude 2^127 or more
wraps, so `fixDot` returns the wrong sign: `vec3.dot(v, v)` for `v = vec3.new(2^47 - 1, 2^47 - 1, 2^47 - 1)` is the
most negative number where the exact result saturates to the most positive. The result is still deterministic
(unsigned wrap is defined), and `fixLength` is unaffected (it reads the sum as unsigned). In the 2^22 run, 131 of
4194304 random dot cases wrapped. Fixed upstream in fixed #59: at pin `4a70c64` the test, renamed
`Layer3bFixDotSaturatesAtTheRangeEnd`, expects saturation, and the oracle requires 0 wraps.

## (2) Against fixed3d built with BOX3D_FIXED_SATURATE

fixed3d (`mas-bandwidth/fixed3d` at `29d5544`) vendors the same `fixed` as moo (`extern/fixed/include` is identical),
and its `b3*` vector, quaternion and transform functions are inline forwarders to fixed's (`include/box3d/fixed_compat.h`).
The comparison binary is kept outside the repository: `compare.cpp` in
`/Volumes/nova/ai/rowan/scratch/moo-land/fixed3d-compare/` on the studio, built against this branch's libraries:

    clang++ -std=c++17 -O2 -fwrapv -ffp-contract=off -DBOX3D_FIXED_SATURATE \
        -Ifixed3d/include -Ifixed3d/extern/fixed/include \
        -Imoo/VM/include -Imoo/Compiler/include -Imoo/Ast/include -Imoo/Common/include \
        fixed3d-compare/compare.cpp build/libLuau.Compiler.a build/libLuau.Bytecode.a build/libLuau.Ast.a \
        build/libLuau.VM.a build/libLuau.Common.a build/libMoo.Fixed.a -o fixed3d-compare

`-fwrapv` makes fixed's plain int64 adds wrap rather than be undefined, so they can be counted. 2^16 cases per
operation, 22 operations:

    fixed3d-compare BOX3D_FIXED_SATURATE ops=22 cases=1441792 differ=136654

Two causes, and only two:

1. **fixed3d's `BOX3D_FIXED_SATURATE` does not reach fixed's inline functions (a fixed3d finding).**
   `fixed_compat.h` includes `fixed/fixed.h` at line 56 and only then, at line 58, defines `FIX_SATURATE` from
   `BOX3D_FIXED_SATURATE`. `fixMul`'s saturation is an `#if defined( FIX_SATURATE )` inside the header, so it is
   already compiled out, and nothing in fixed3d's build defines `FIX_SATURATE` otherwise. A fixed3d build "with
   BOX3D_FIXED_SATURATE" therefore wraps `fixMul`, `fixMulSV`, `fixCross`, `fixVecMul` exactly as the default build
   does. Seen as `vec3 * number` differing on 32788 of 65536 full-range cases and `vec3.cross` on 35734 (with 2^40
   raw components a product reaches 2^64 raw). Rebuilt with `-DFIX_SATURATE` as well, those differences are 0.
2. **fixed's `fixVecAdd`/`fixVecSub` are plain int64 (the upstream question DESIGN.md names).** With saturation
   really on, the differences that remain are exactly moo's saturating `+` and `-` against fixed's plain adds:

       vec3 + vec3 (full range)                                     differ=13472
       vec3 - vec3 (full range)                                     differ=13858
       transform * transform (identity rotation, full-range position) differ=13472
       transform.point (identity rotation, full range)              differ=13472
       transform.invpoint (identity rotation, full range)           differ=13858
       everything else (17 operations, in range or full range)      differ=0

   Every one is a case where fixed's add overflows int64 (undefined in C without `-fwrapv`); inside the range moo and
   fixed3d agree bit for bit.

## (4) The nightly step

`.github/workflows/nightly.yml` sets `MOO_ORACLE_3B_N: "4194304"` (2^22 per operation) beside `MOO_ORACLE_N` in both
the Linux/macOS and the Windows jobs; the existing `Moo.Test` step runs it. It has its own variable because the layer 3
oracle's 2^24 would make 22 object operations take hours.

## Questions for upstream (Glenn's)

- fixed3d: define `FIX_SATURATE` before `fixed_compat.h` includes `fixed/fixed.h` (or in the build), or
  `BOX3D_FIXED_SATURATE` does nothing for fixed's arithmetic.
- fixed: saturating `fixVecAdd`/`fixVecSub` and the sums inside `fixCross`, `fixRotateVector` and `fixMulAdd` under
  `FIX_SATURATE` (DESIGN.md "Saturation, stated precisely"), and a `fixDotRaw` that does not wrap at the top of the
  range.

## Not done

- Item (3) and (5), the fold of the layer 3b notes into DESIGN.md and DETERMINISM.md and the lock-review input, are
  the sibling card's.
- The comparison is not committed (by the card's rule); it is not in CI.
