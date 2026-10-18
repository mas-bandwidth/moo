#!/bin/sh
# moo: every skipped or moved upstream test says why in moo's words, and a reason that names a
# replacement resolves (docs/DESIGN.md "Layers", layer 5; docs/DETERMINISM.md "Status").
#
# usage: tools/moo-check-skips.sh [root]
#   root defaults to the checkout this script is in; the doctest case passes a fixture tree.
#
# It lists every skipped doctest case: a doctest skip decorator, a #if 0 block that holds a
# TEST_CASE, and an early return that carries a moo: reason (the codegen cases moo excludes).
# It also lists every moved conformance script under tests/conformance/moo. Each record needs a
# "moo:" reason on its line or the line above (a moved script needs one in its comments). A
# reason that says "See tests/X" or "(tests/X Name)" must name a file that exists and, when it
# names a case, that grep finds the case.
#
# The exit status is 1 when a record lacks a reason or names something missing; the summary is
# printed either way, so a run tells the whole story.

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ "$#" -ge 1 ]; then
    root=$1
fi
if [ ! -d "$root" ]; then
    echo "moo-check-skips: no such tree: $root" >&2
    exit 2
fi
cd "$root"

work=$(mktemp "${TMPDIR:-.}/moo-check-skips.XXXXXX") || exit 2
trap 'rm -f "$work" "$work.rec" "$work.fail"' EXIT HUP INT TERM
records=$work.rec
failures=$work.fail
: >"$records"
: >"$failures"

if [ ! -d tests ]; then
    echo "moo-check-skips: $root has no tests directory" >&2
    exit 2
fi

# The C++ side. One awk per file keeps the previous line for the "line above" rule.
find tests -type f -name '*.cpp' | LC_ALL=C sort | while IFS= read -r file; do
    awk -v file="$file" '
        function has_reason(text) { return index(text, "moo:") > 0 }
        function emit(at, kind, reason) {
            gsub(/\t/, " ", reason)
            printf "%s\t%d\t%s\t%s\n", file, at, kind, reason
        }
        function reason_for(line, above) {
            if (has_reason(line)) return line
            if (has_reason(above)) return above
            return ""
        }
        {
            line = $0

            if (inblock) {
                if (line ~ /^[ \t]*#[ \t]*(if|ifdef|ifndef)([ \t]|$)/)
                    depth++
                if (line ~ /^[ \t]*#[ \t]*endif([ \t]|$)/) {
                    depth--
                    if (depth == 0) {
                        if (blockhas)
                            emit(blockline, "#if 0 block", blockreason)
                        inblock = 0
                    }
                }
                if (line ~ /^[ \t]*TEST_CASE/)
                    blockhas = 1
                previous2 = previous
                previous = line
                next
            }

            if (line ~ /^[ \t]*#[ \t]*if[ \t]+0([ \t]|$)/) {
                inblock = 1
                depth = 1
                blockline = FNR
                blockhas = 0
                blockreason = reason_for(line, previous)
                previous2 = previous
                previous = line
                next
            }

            if (line ~ /doctest::skip/)
                emit(FNR, "doctest skip", reason_for(line, previous))

            # An early return marked moo: is a skipped case too; the marker is above the if.
            if (line ~ /^[ \t]*return;[ \t]*$/ && previous ~ /^[ \t]*if[ \t]*\(/) {
                if (has_reason(previous) || has_reason(previous2) || has_reason(line))
                    emit(FNR - 1, "early return", reason_for(previous, previous2) != "" ? reason_for(previous, previous2) : line)
            }

            previous2 = previous
            previous = line
        }
        END {
            if (inblock && blockhas)
                emit(blockline, "#if 0 block", blockreason)
        }
    ' "$file"
done >>"$records"

# A moved conformance script: its reason is the "-- moo:" or "moo-specific" note in the file.
find tests/conformance/moo -type f -name '*.luau' 2>/dev/null | LC_ALL=C sort | while IFS= read -r file; do
    reason=$(grep -m1 -E '^[[:space:]]*--.*(moo:|moo-specific)' "$file" || true)
    printf '%s\t0\tmoved script\t%s\n' "$file" "$reason" >>"$records"
done

# Check each record's reason and every tests/ reference inside it.
while IFS='	' read -r file line kind reason; do
    if [ -z "$reason" ]; then
        if [ "$line" -eq 0 ]; then
            echo "$file: $kind has no moo: reason" >>"$failures"
        else
            echo "$file:$line: $kind has no moo: reason" >>"$failures"
        fi
        continue
    fi

    text=$reason
    if [ "$kind" = "moved script" ]; then
        text=$(grep -E '^[[:space:]]*--' "$file" || true)
    fi

    # "Replaces upstream tests/X" names the file that is gone, not a replacement. A replacement
    # ("See tests/X", "(tests/X Name)") must resolve.
    printf '%s\n' "$text" \
        | grep -v -e 'upstream' -e 'Upstream' -e 'Replaces' \
        | grep -oE 'tests/[A-Za-z0-9_./-]+([[:space:]]+[A-Z][A-Za-z0-9_]*)?' \
        | LC_ALL=C sort -u | while IFS= read -r ref; do
            ref=$(printf '%s' "$ref" | sed 's/[.,;:)]*$//')
            case "$ref" in
                *' '*)
                    path=${ref%% *}
                    name=${ref#* }
                    ;;
                *)
                    path=$ref
                    name=
                    ;;
            esac
            if [ ! -e "$path" ]; then
                echo "$file:$line: $kind: reason names $path, which does not exist" >>"$failures"
            elif [ -n "$name" ] && ! grep -rq -- "$name" "$path" 2>/dev/null; then
                echo "$file:$line: $kind: reason names $name, which grep does not find in $path" >>"$failures"
            fi
        done
done <"$records"

total=$(wc -l <"$records" | tr -d ' ')
failed=$(wc -l <"$failures" | tr -d ' ')

echo "moo-check-skips: $total skipped or moved records, $failed without a reason or with a missing replacement"
echo "by file:"
awk -F '\t' '{ count[$1]++ } END { for (file in count) printf "%4d  %s\n", count[file], file }' "$records" | LC_ALL=C sort -k2
echo "by kind:"
awk -F '\t' '{ count[$3]++ } END { for (kind in count) printf "%4d  %s\n", count[kind], kind }' "$records" | LC_ALL=C sort -k2

if [ "$failed" -ne 0 ]; then
    echo "failures:"
    sed 's/^/  /' "$failures"
    exit 1
fi

echo "moo-check-skips: every skipped or moved case names a moo: reason and its replacement"
