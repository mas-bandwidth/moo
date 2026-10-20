# moo

<img width="559" height="510" alt="image" src="https://github.com/user-attachments/assets/f8f8e7c8-04fe-40a3-8050-f9f698ab4e04" />

moo is a deterministic, fixed-point fork of [Luau](https://github.com/luau-lang/luau).

- **Home:** https://github.com/mas-bandwidth/moo
- **Forked from:** Luau **0.741** (upstream tag `0.741`, commit `2d88f9b5facaa9e4ec91247b39ae8251bd49a244`).
  The upstream source archive for that tag was checked against
  sha256 `deee653d0f5971b809ef67f50dbe8109ad369d7575e344cfe2013774fed2f4c6`, and the tree
  this repository starts from is identical to it. Full upstream history is kept.

## What moo changes

moo makes one promise Luau does not: **the same program with the same inputs gives
bit-identical results, and the same sequence of observable effects, on every platform and
compiler, for every construct.** To keep it, moo will:

1. remove every non-deterministic source in the VM and standard library (object hashing by
   address, GC pacing by clock, `os` and time, address-revealing `tostring`, platform libm);
2. make `number` a Q48.16 fixed-point value, so floating point never enters the VM.

Everything else stays as close to Luau as possible: syntax, tools and the C API.

Layers 1-5 are built. The conformance record is the program-by-program matrix in
[docs/DETERMINISM.md](docs/DETERMINISM.md), "Status"; the other promises are
[docs/DESIGN.md](docs/DESIGN.md), "Layers". Every source of non-determinism found so far, with
its fix, is in [docs/DETERMINISM.md](docs/DETERMINISM.md).

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
    build/moo tests/conformance/assert.luau

Supported for moo: Linux (gcc and clang), macOS arm64 and Windows (MSVC and clang-cl),
64-bit only; Windows is nightly-only. moo compiles
everything with `-ffp-contract=off`, so the C++ compiler never fuses a multiply and an add
into one FMA (which rounds differently). The upstream `Makefile` is kept and builds the same
`moo*` binaries with the same flag.

## Tests and CI

Luau's own suites run unmodified:

- `Luau.Conformance` (the `tests/conformance` scripts),
- `Luau.UnitTest` (the compiler, analyzer and codegen unit tests),
- `Luau.CLI.Test` (the REPL and `require` tests; run it from the repository root).

moo's own tests are `Moo.Test`: the determinism cases and the fixed-point operators,
literals and formatting in `tests/Moo.test.cpp`, `tests/MooNumber.test.cpp` and
`tests/Determinism.test.cpp`.

CI (`.github/workflows/`):

- **`ci.yml` `push-gate`, required on every push and pull request:** Linux clang. It builds
  only `Moo.Test` and `Luau.VM`, runs `Moo.Test`, checks `extern/fixed` against its pin, and
  checks that the VM archive reads no clock. With the compiler cache a warm run is under a
  minute; a change that touches a VM header rebuilds the VM and nothing else.
- **`ci.yml` `full`, on pull requests, not required:** Linux clang. It builds and runs
  `Luau.Conformance`, `Luau.UnitTest` and `Luau.CLI.Test`, smoke-runs `moo`, `moo-analyze`
  and `moo-compile`, and checks that native codegen is refused.
- **`nightly.yml`, nightly and on demand:** the matrix on Linux gcc, Linux clang and macOS
  arm64 runs `Moo.Test`, `Luau.Conformance` (also at `-O2`), `Luau.UnitTest` and
  `Luau.CLI.Test`, the no-clock check and the smoke runs; Windows MSVC, nightly-only, runs
  `Moo.Test`, `Luau.Conformance` and `Luau.UnitTest`; a separate job model-checks the `tla`
  cases with TLC 1.8.0.

Upstream's own workflows (`build.yml`, `release.yml`, `new-release.yml`, `benchmark.yml`)
are removed in moo; they target Luau's release process and runners.

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

## Consumers

[Virtual Go](https://github.com/robotgamedevelopers/virtualgo) is the first consumer: its
`script/` host moves from Luau to moo. The layer 6 adoption check (its own `script/` CTest
suite, run nightly against moo at the job's commit) is held until Virtual Go's
`vg-script-repin-moo-l5` card pins moo at or past the layer 5 lock; see
[docs/DESIGN.md](docs/DESIGN.md), "Layers".

## License

moo is MIT licensed, the same as Luau.

- [LICENSE.txt](LICENSE.txt) is Luau's MIT license and copyright notice, kept intact.
- [lua_LICENSE.txt](lua_LICENSE.txt) is Lua's, kept intact.
- [LICENSE-moo.txt](LICENSE-moo.txt) covers moo's own changes, also MIT.
