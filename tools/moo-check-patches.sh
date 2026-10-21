#!/bin/sh
# moo: the patch inventory is the merge checklist (docs/DESIGN.md "Upstream merges"). Every
# upstream file moo modifies or deletes must appear in docs/PATCHES.md, and every row in that
# file must still differ from the fork.
#
# usage: tools/moo-check-patches.sh [root]
#   root defaults to the checkout this script is in; the doctest case passes the same tree.
#
# The comparison is against the working tree, not HEAD, so an uncommitted edit to an unlisted
# upstream file fails the check too. The fork commit (Luau 0.741) must be in the history: CI's
# checkout uses fetch-depth: 0. The exit status is 1 for an unlisted upstream change, a stale
# row, or a missing fork commit; the summary prints either way.

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ "$#" -ge 1 ]; then
    root=$1
fi
if [ ! -d "$root" ]; then
    echo "moo-check-patches: no such tree: $root" >&2
    exit 2
fi
cd "$root"

# Luau 0.741, the commit README.md names as the fork point.
fork=2d88f9b5facaa9e4ec91247b39ae8251bd49a244
inventory=docs/PATCHES.md

if [ ! -f "$inventory" ]; then
    echo "moo-check-patches: $inventory does not exist" >&2
    exit 1
fi

if ! git rev-parse --git-dir >/dev/null 2>&1; then
    echo "moo-check-patches: $root is not a git checkout" >&2
    exit 2
fi
if ! git cat-file -e "$fork^{commit}" 2>/dev/null; then
    echo "moo-check-patches: the fork commit $fork is not in the history; fetch full history (actions/checkout fetch-depth: 0)" >&2
    exit 1
fi

work=$(mktemp "${TMPDIR:-.}/moo-check-patches.XXXXXX") || exit 2
trap 'rm -f "$work" "$work.added" "$work.modified" "$work.listed" "$work.unlisted" "$work.stale"' EXIT HUP INT TERM

git diff --name-status --no-renames "$fork" >"$work"

added=$work.added
modified=$work.modified
listed=$work.listed
unlisted=$work.unlisted
stale=$work.stale

awk -F '\t' '$1 == "A" { print $2 }' "$work" | LC_ALL=C sort -u >"$added"
awk -F '\t' '$1 != "A" { print $2 }' "$work" | LC_ALL=C sort -u >"$modified"

# A row is a table line whose first cell is one backticked path. moo-added files are a bullet
# list by directory, so they never look like an upstream row.
awk '/^[[:space:]]*\|[[:space:]]*`[^`]+`[[:space:]]*\|/ {
    line = $0
    sub(/^[^|]*\|[[:space:]]*`/, "", line)
    sub(/`.*$/, "", line)
    print line
}' "$inventory" | LC_ALL=C sort -u >"$listed"

LC_ALL=C comm -23 "$modified" "$listed" >"$unlisted"
LC_ALL=C comm -13 "$modified" "$listed" >"$stale"

nmodified=$(wc -l <"$modified" | tr -d ' ')
nadded=$(wc -l <"$added" | tr -d ' ')
nunlisted=$(wc -l <"$unlisted" | tr -d ' ')
nstale=$(wc -l <"$stale" | tr -d ' ')

echo "moo-check-patches: upstream files moo modifies or deletes ($nmodified):"
sed 's/^/  /' "$modified"
echo "moo-check-patches: moo-added files ($nadded):"
sed 's/^/  /' "$added"

failed=0
while IFS= read -r file; do
    [ -n "$file" ] || continue
    echo "moo-check-patches: $file is modified or deleted upstream but is not in $inventory"
    failed=1
done <"$unlisted"
while IFS= read -r file; do
    [ -n "$file" ] || continue
    echo "moo-check-patches: $inventory lists $file, which no longer differs from the fork"
    failed=1
done <"$stale"

if [ "$failed" -ne 0 ]; then
    echo "moo-check-patches: $nmodified upstream files modified or deleted, $nadded added; $nunlisted unlisted, $nstale stale"
    exit 1
fi
echo "moo-check-patches: $nmodified upstream files modified or deleted, $nadded added; every one is in $inventory"
