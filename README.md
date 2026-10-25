# moo

<img src="docs/logo/moo-logo.svg" width="320" alt="moo">

moo is a deterministic, fixed-point fork of [Luau](https://github.com/luau-lang/luau).

- **Home:** https://github.com/mas-bandwidth/moo
- **Forked from:** Luau **0.741** (upstream tag `0.741`, commit `2d88f9b5facaa9e4ec91247b39ae8251bd49a244`).
  The upstream source archive for that tag was checked against
  sha256 `deee653d0f5971b809ef67f50dbe8109ad369d7575e344cfe2013774fed2f4c6`, and the tree
  this repository starts from is identical to it. Full upstream history is kept.

## What moo changes

moo makes one promise Luau does not: **the same program with the same inputs gives
bit-identical results, and the same sequence of observable effects, on every supported platform
and compiler, for every construct.** Inputs are the source or bytecode, the values the host
passes in through the C API (including the `math.random` seed) and the results of host
functions; anything else that could influence a run (addresses, clocks, the environment, the
locale, the CPU, the C library) is treated as a defect. The full statement, and every source of
non-determinism found in Luau 0.741 with its fix, is [docs/DETERMINISM.md](docs/DETERMINISM.md).

To keep it, moo:

- hashes objects by a per-VM creation number instead of their address, so table iteration
  order is deterministic, and `tostring` of a table prints `table: #123`, not an address;
- paces the GC by allocated bytes with no clock, and runs userdata destructors in creation
  order at a defined point (a link-time check refuses any clock in the VM);
- removes the `os` library (time is an input), seeds `math.random` with a constant unless the
  host calls `moo_setrandomseed`, and limits the CLI's `collectgarbage` to `"collect"` and `"count"`;
- turns native code generation off (interpreter only);
- makes `number` Q48.16 fixed point, so floating point never enters the VM.

### The number model

`number` is a signed 64-bit value with 16 fraction bits: the raw `fixed_t` of the C library
[fixed](https://github.com/mas-bandwidth/fixed), vendored under `extern/fixed` at a pinned
commit. This is the main thing that differs from Luau:

- **Resolution and range.** Steps of 2^-16 (about 0.0000153); magnitudes up to just under
  2^47 (about 1.4e14). Every integer below 2^47 in magnitude is exact.
- **No floats, NaN or infinity.** `math.huge` is the largest value, `math.isnan` and
  `math.isinf` are always false, and `a / 0`, `a // 0` and `a % 0` raise `division by zero`.
- **Overflow saturates** to the largest or smallest value instead of wrapping or going to
  infinity. Checked builds (assertions on, or `MOO_OVERFLOW_ERRORS=ON`) raise
  `number overflow in '<op>'` instead.
- **Rounding.** `+`, `-`, `%` and comparisons are exact; `*` rounds half up and `/`
  truncates toward zero, as `fixed` does. Decimal literals and `tonumber` round once to the
  nearest value, ties to even (`0.1 + 0.2 == 0.3` holds, and `1/3` prints `0.33333`); a literal
  out of range is a compile error.
- **Integer arguments** (`string.sub` positions, `bit32`, `buffer` offsets, `%d`, and so on)
  must be integer-valued; a fraction is the error `number has no integer representation`,
  where Luau truncates silently.
- **The math library** is computed in fixed point (no libm); each function's error is measured
  against a correctly rounded reference ([docs/DESIGN.md](docs/DESIGN.md), "math library").
  `tostring` prints the shortest decimal that reads back to the same value.
- **Floats exist only at the boundary:** `moo_fromdouble`/`moo_todouble` in the C API, and
  `buffer` and `string.pack` float formats, convert with one exact integer rule.
- **`vector` and `integer` are gone.** Luau's float `vector` type is replaced by three
  immutable fixed-point value types, `vec3`, `quat` and `transform`, with operators, libraries,
  C API functions and analyzer types; their results are checked bit for bit against `fixed`'s
  and fixed3d's C. Luau's experimental `integer` type is not opened.
- **Bytecode** has its own moo version and stores raw Q48.16 constants; the VM refuses Luau
  bytecode by name.

The design, with what is built and how it is checked, is [docs/DESIGN.md](docs/DESIGN.md).
What is not done yet is listed in [ROADMAP.md](ROADMAP.md).

## What the fork renames

The rename is kept minimal so upstream merges stay clean.

| Luau | moo |
|---|---|
| `luau` (REPL / runner) | `moo` |
| `luau-analyze` | `moo-analyze` |
| `luau-compile` | `moo-compile` |
| `luau-ast`, `luau-reduce`, `luau-bytecode` | `moo-ast`, `moo-reduce`, `moo-bytecode` |
| source extension `.luau` | `.moo` (`.luau` and `.lua` are still read) |

`require` resolution, the CLI's file lookup and directory traversal try `.moo` first, then
`.luau`, then `.lua` (and `init.moo` before `init.luau`). Two candidates for one module path
are reported as ambiguous, as upstream already does for `.luau`/`.lua`.

What is **not** renamed: the C API keeps Luau's `lua_`/`luaL_` names, the CMake targets keep
their `Luau.*` names, the C++ namespace stays `Luau`, and the configuration file stays
`.config.luau`. Source is not mass-renamed.

## Building

    cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build build -j
    build/moo --version
    build/moo tests/conformance/assert.luau

`moo --version` prints moo's version and the Luau release it is forked from; the C API has
`MOO_VERSION` in `VM/include/moo.h`. Releases are listed in [CHANGELOG.md](CHANGELOG.md).

Supported for moo: Linux (gcc and clang), macOS arm64 and Windows (MSVC and clang-cl),
64-bit only; Windows is nightly-only. With gcc and clang moo compiles everything with
`-ffp-contract=off`, so the compiler never fuses a multiply and an add into one FMA (which
rounds differently); on Windows only the vendored `fixed` gets the flag under clang-cl. The
VM computes no floating point either way: the number is integer arithmetic. The upstream `Makefile` is kept and builds the same
`moo*` binaries with the same flag.

## Tests and CI

Luau's own suites run, with every case moo skips or moves marked with its `moo:` reason
(`tools/moo-check-skips.sh` lists them):

- `Luau.Conformance` (the `tests/conformance` scripts),
- `Luau.UnitTest` (the compiler and analyzer unit tests; the codegen tests are not built),
- `Luau.CLI.Test` (the REPL and `require` tests; run it from the repository root).

moo's own tests are `Moo.Test`: the determinism suite (`tests/Determinism.test.cpp`, whose
state and effect hashes are pinned in `tests/determinism/expected.txt`), the fixed-point
operators, literals and formatting (`tests/Moo.test.cpp`, `tests/MooNumber.test.cpp`), and one
file per change under `tests/moo/`, including the oracles that check numbers, the math library
and `vec3`/`quat`/`transform` bit for bit against `fixed`'s C.

CI (`.github/workflows/`):

- **`ci.yml` `push-gate`, on every push to `main` and every pull request into it (and required
  by `release.yml` on a tagged commit):** Linux clang. It builds
  only `Moo.Test` and `Luau.VM`, runs `Moo.Test`, checks `extern/fixed` against its pin, and
  checks that the VM archive reads no clock. With the compiler cache a run takes about one to
  one and a half minutes; a change that touches a VM header rebuilds the VM and nothing else.
- **`ci.yml` `full`, on pull requests, not required:** Linux clang. It builds and runs
  `Luau.Conformance`, `Luau.UnitTest` and `Luau.CLI.Test`, smoke-runs `moo`, `moo-analyze`
  and `moo-compile`, checks that native codegen is refused, and compares `vec3`, `quat` and
  `transform` with fixed3d's own functions (`tools/moo-fixed3d-compare.sh`).
- **`nightly.yml`, nightly and on demand:** the matrix on Linux gcc, Linux clang and macOS
  arm64 runs `Moo.Test`, `Luau.Conformance` (also at `-O2`), `Luau.UnitTest` and
  `Luau.CLI.Test`, the no-clock check and the smoke runs; Windows MSVC and clang-cl, nightly-only, run
  `Moo.Test`, `Luau.Conformance` and `Luau.UnitTest`; a separate job model-checks the `tla`
  cases with TLC 1.8.0, and another rebuilds and compares the CORE-MATH math oracle.

- **`release.yml`, on a `v*` tag:** publishes the GitHub release from the tag the release
  tool cut, after checking that `MOO_VERSION` is the tag's version, `CHANGELOG.md` has its
  section, and `push-gate` passed on the tagged commit.

Upstream's own workflows (`build.yml`, `release.yml`, `new-release.yml`, `benchmark.yml`)
are removed in moo; they target Luau's release process and runners. moo's `release.yml`
replaces upstream's file of the same name.

## Taking upstream changes

The `upstream` remote is Luau:

    git remote add upstream https://github.com/luau-lang/luau.git
    git fetch upstream --tags

Upstream is taken **once per upstream release**, never commit by commit:

1. Merge the release tag (for example `git merge 0.742`) into a branch. Keep the merge
   commit, so `git log --first-parent` shows exactly which Luau release moo is on.
2. Rebase moo's patches on top as needed, resolving conflicts in moo's favour only where moo
   deliberately differs (the rename, the removed workflows, and later the fixed-point VM).
3. Rerun the full conformance suite (and, from layer 5, the determinism suite) on every
   platform. The merge lands only when it is green.
4. Update the "Forked from" line above.

moo keeps its patches small and in few files for exactly this reason; see
[docs/DESIGN.md](docs/DESIGN.md), "Upstream merges".

## License

moo is MIT licensed, the same as Luau.

- [LICENSE.txt](LICENSE.txt) is Luau's MIT license and copyright notice, kept intact.
- [lua_LICENSE.txt](lua_LICENSE.txt) is Lua's, kept intact.
- [LICENSE-moo.txt](LICENSE-moo.txt) covers moo's own changes, also MIT, copyright Más Bandwidth LLC.
