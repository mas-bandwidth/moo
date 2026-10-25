#!/bin/sh
# moo: prints the CHANGELOG.md section of one release, the body of its GitHub release
# (.github/workflows/release.yml). A section starts at a line `## <version>` (the hand-written
# notes) or `## <version> — <date>` (the record the release tool writes when it cuts the tag) and
# runs to the next `## v` line. Every section of the version is printed, in file order.
#
# usage: tools/moo-release-notes.sh <version> [changelog]
#   version is the tag, such as v1.0.0. Exit 1 when the changelog has no section for it.
#   With --check-version, also exit 1 unless MOO_VERSION in VM/include/moo.h is the version
#   without its leading v:  tools/moo-release-notes.sh --check-version <version> [changelog]

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

check=0
if [ "${1:-}" = "--check-version" ]; then
    check=1
    shift
fi
if [ "$#" -lt 1 ]; then
    echo "usage: tools/moo-release-notes.sh [--check-version] <version> [changelog]" >&2
    exit 2
fi
version=$1
changelog=${2:-$root/CHANGELOG.md}

if [ "$check" -eq 1 ]; then
    code=$(sed -n 's/^#define MOO_VERSION "\(.*\)"$/\1/p' "$root/VM/include/moo.h")
    if [ "v$code" != "$version" ]; then
        echo "moo-release-notes: MOO_VERSION is \"$code\" in VM/include/moo.h, not ${version#v}" >&2
        exit 1
    fi
fi

notes=$(awk -v v="$version" '
    /^## / {
        heading = substr($0, 4)
        on = (heading == v || index(heading, v " ") == 1)
        if (on) found = 1
    }
    on { print }
    END { if (!found) exit 1 }
' "$changelog") || {
    echo "moo-release-notes: $changelog has no section for $version" >&2
    exit 1
}
printf '%s\n' "$notes"
