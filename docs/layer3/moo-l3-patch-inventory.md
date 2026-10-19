# moo-l3-patch-inventory

The patch inventory is the merge checklist (docs/DESIGN.md "Upstream merges"): every upstream
file moo modifies or deletes gets a row in `docs/PATCHES.md` saying why, and a check fails the
push gate when the tree and the inventory disagree. The inventory is written once, against
layer 3's final tree, because this card runs after every other layer 3 code card.

## Decision

- **The check compares the working tree with the fork commit**, not `HEAD`. That is what makes an
  uncommitted edit to an unlisted upstream file fail too, as the card's DONE-WHEN asks; a commit
  that is already on the branch differs the same way. The fork commit is Luau 0.741,
  `2d88f9b5facaa9e4ec91247b39ae8251bd49a244`, which README.md names and which is in this
  repository's history.
- **Modified or deleted upstream files are one row each**, grouped by area, because they are the
  only files a merge can conflict with. moo-added files are listed by directory: upstream never
  edits them, so they are not merge work.
- **The row's reason names a section**: `docs/DESIGN.md "<section>"` or `docs/DETERMINISM.md, N`.
  The reason was read from each file's `moo:` comments and `git log` on that file, never guessed.
- **CI checks out full history in the push gate** (`fetch-depth: 0`) so the fork commit is
  present; the full job does not run this check and does not need it. Everything else in the push
  gate is unchanged.

## What changed

- `tools/moo-check-patches.sh` (new, executable, POSIX sh): `git diff --name-status --no-renames
  <fork>` against the working tree, split into added and modified/deleted; it extracts the
  backticked first cell of every table row in `docs/PATCHES.md`, then uses `comm` to report an
  upstream file with no row and a row that no longer differs. It prints every added file and every
  modified/deleted upstream file, then the summary line. It exits 1 on either set, or when the
  fork commit is missing (a shallow checkout), and prints the `fetch-depth: 0` remedy. It uses
  plain `git`, `awk`, `sort`, `comm` and `sed`, and takes 0.12 s locally (measured with `time`,
  `real 0m0.115s`), so it does not move the one-minute push gate.
- `docs/PATCHES.md` (new): 91 rows (85 modified, 6 deleted) under VM, Compiler, Ast, Analysis,
  Bytecode, CLI, Config, CMake/Makefile, fuzz, tests, workflows and README, then the moo-added
  files by directory.
- `.github/workflows/ci.yml`: the push gate's `actions/checkout@v4` gains `fetch-depth: 0`, and a
  step `every upstream change is in docs/PATCHES.md` runs `tools/moo-check-patches.sh` after the
  checkout. The cost is the full-history fetch; the comparison itself is the 0.12 s above and no
  other step changes.
- `tests/moo/moo-l3-patch-inventory.test.cpp` (new): `MooL3PatchInventoryRows` parses the table,
  requires a backticked path, an area heading, a reason naming DESIGN.md or DETERMINISM.md, no
  duplicate path, and all twelve areas; `MooL3PatchInventoryGate` requires the executable script,
  the fork commit in the script, `fetch-depth: 0` and the script step in `ci.yml`, and that every
  moo-added directory in the list exists. The set comparison needs full history, so it stays with
  the script in the push gate; the nightly Moo.Test checkout is shallow.
- `tests/conformance/moo/l3/moo-l3-patch-inventory.luau` (new): `MooL3Scripts` runs it and it
  returns "OK"; it samples the runtime side of the inventory's removals (`os`, `vector` and
  `integer` are absent; `0.5` is the raw value `32768`) and that the loader refuses a non-moo
  bytecode blob.
- This note.

## Sentences to change when the layer closes

docs/DESIGN.md, "Upstream merges", this bullet:

> - **A patch inventory** (`docs/PATCHES.md`, written in layer 2) lists every upstream file moo
>   modifies and why. It is the merge checklist, and it is checked: CI fails if a commit touches
>   an upstream file that is not in it.

must become:

> - **A patch inventory** (`docs/PATCHES.md`, written in layer 3 against the final layer 3 tree)
>   lists every upstream file moo modifies or deletes and why; moo-added files are listed by
>   directory. It is the merge checklist, and it is checked: the push gate runs
>   `tools/moo-check-patches.sh` (with `fetch-depth: 0`) and fails when an upstream file is changed
>   without a row, or a row no longer differs.

docs/DESIGN.md, "Layer 3 as built": no existing sentence mentions the inventory, so add a bullet:

> - **The merge checklist is built.** `docs/PATCHES.md` lists every upstream file layer 3 modifies
>   or deletes with the DESIGN.md or DETERMINISM.md section it implements, and
>   `tools/moo-check-patches.sh` refuses a change that is not in it.

docs/DETERMINISM.md: no sentence must change. The inventory records the fixes the status table
already names; it adds no determinism rule and changes no hash or frozen value.
