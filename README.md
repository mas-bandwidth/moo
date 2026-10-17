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

None of that is built yet. This tree is **layer 1, the fork**: Luau 0.741, the rename, and CI
on Luau's unmodified test suite. The plan is in [docs/DESIGN.md](docs/DESIGN.md), and every source of
non-determinism found so far, with its proposed fix, is in
[docs/DETERMINISM.md](docs/DETERMINISM.md).

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

Supported for moo: Linux (gcc and clang) and macOS arm64 (Apple clang). moo compiles
everything with `-ffp-contract=off`, so the C++ compiler never fuses a multiply and an add
into one FMA (which rounds differently). The upstream `Makefile` is kept and builds the same
`moo*` binaries with the same flag.

## Tests and CI

Luau's own suites run unmodified:

- `Luau.Conformance` (the `tests/conformance` scripts),
- `Luau.UnitTest` (the compiler, analyzer and codegen unit tests),
- `Luau.CLI.Test` (the REPL and `require` tests; run it from the repository root).

CI (`.github/workflows/`):

- **`ci.yml`, on every push and pull request:** Linux clang, all three suites. A cold build
  of Luau's analyzer alone takes several minutes, so this tier uses a compiler cache; a warm
  run is about a minute, and only changes that touch the analyzer or a widely included header
  pay for a cold rebuild.
- **`nightly.yml`, nightly and on demand:** the full matrix (Linux gcc, Linux clang, macOS
  arm64), all three suites, plus Conformance at `-O2` and a smoke run of each moo binary.

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

## License

moo is MIT licensed, the same as Luau.

- [LICENSE.txt](LICENSE.txt) is Luau's MIT license and copyright notice, kept intact.
- [lua_LICENSE.txt](lua_LICENSE.txt) is Lua's, kept intact.
- [LICENSE-moo.txt](LICENSE-moo.txt) covers moo's own changes, also MIT.
