#!/bin/sh
# moo: no temporary double bridge remains (docs/DESIGN.md, "Layer 3 as built", "TEMPORARY bridges").
# This script holds that: it lists every moo_numbridgetodouble / moo_numbridgefromdouble call and
# every TEMPORARY marker in VM/, Compiler/, Common/, Ast/ and Bytecode/, and today every count is 0.
# A hit outside the allowlist is a finding: exit 1, naming each. Never widen the allowlist to make a
# run green.
#
# The allowlist is the set of places the bridge was last allowed, the math library and ^, all now
# empty:
#   VM/src/lmathlib.cpp      the math library
#   VM/src/lbuiltins.cpp     the math fastcalls (a hit inside a luauF_vector* body is a finding)
#   VM/src/lnumutils.h       the line defining luai_numpow
#   VM/src/lgcdebug.cpp      host debug output
# A definition is not a call: in Common/include/moo_number.h a line that begins with "inline"
# and declares one of the two names declares the bridge, so the script records it as a
# definition and does not count it. A hit that is only inside a comment is not a call either.
# A TEMPORARY marker beside an allowlisted bridge line is allowed with it.
#
# The scan population is the built sources. A .cpp translation unit is scanned only when
# Sources.cmake names it; every header is scanned, because a header reaches the build through an
# includer. A translation unit that is out of the build stays in the tree unbuilt and cannot hold
# a bridge in the product: VM/src/lveclib.cpp, the removed float vector library (Sources.cmake;
# docs/DESIGN.md, "vec3, quat and transform"). This is not an allowlist entry.
#
# usage: tools/moo-check-bridges.sh [root]
#   root defaults to the checkout this script is in.
#
# The exit status is 1 when any call or marker is outside the allowlist; the per-file counts and
# summary are printed either way, so a run tells the whole story.

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ "$#" -ge 1 ]; then
    root=$1
fi
if [ ! -d "$root" ]; then
    echo "moo-check-bridges: no such tree: $root" >&2
    exit 2
fi
if ! git -C "$root" rev-parse --git-dir >/dev/null 2>&1; then
    echo "moo-check-bridges: $root is not a git checkout" >&2
    exit 2
fi

if [ ! -f "$root/Sources.cmake" ]; then
    echo "moo-check-bridges: $root has no Sources.cmake" >&2
    exit 2
fi

work=$(mktemp "${TMPDIR:-.}/moo-check-bridges.XXXXXX") || exit 2
trap 'rm -f "$work" "$work.calls" "$work.marks" "$work.all" "$work.built"' EXIT HUP INT TERM
calls=$work.calls
marks=$work.marks
all=$work.all
built=$work.built
: >"$calls"
: >"$marks"
: >"$all"

# the .cpp translation units in the build, by the same list CMakeLists.txt includes
grep -oE '(VM|Compiler|Common|Ast|Bytecode)/[A-Za-z0-9_./-]+\.cpp' "$root/Sources.cmake" \
    | LC_ALL=C sort -u >"$built"

git -C "$root" grep -n -E 'moo_numbridgetodouble|moo_numbridgefromdouble' -- \
    VM Compiler Common Ast Bytecode >"$calls" 2>/dev/null || true
git -C "$root" grep -n 'TEMPORARY' -- \
    VM Compiler Common Ast Bytecode >"$marks" 2>/dev/null || true

# a translation unit that is not in the build is not a remaining bridge in the product
in_build()
{
    case "$1" in
        *.cpp) grep -qxF -- "$1" "$built" ;;
        *) return 0 ;;
    esac
}

# the last luauF_* function whose definition is at or above a line in lbuiltins.cpp
func_at()
{
    file=$1
    line=$2
    awk -v target="$line" '
        match($0, /luauF_[A-Za-z0-9_]+[ \t]*\(/) {
            fn = substr($0, RSTART, RLENGTH)
            sub(/[ \t]*\($/, "", fn)
        }
        NR == target { print fn; exit }
    ' "$root/$file"
}

# a call is a hit in code, not in a comment; a definition in moo_number.h is not a call
while IFS= read -r rec; do
    [ -n "$rec" ] || continue
    file=${rec%%:*}
    rest=${rec#*:}
    line=${rest%%:*}
    text=${rest#*:}
    in_build "$file" || continue
    code=$(printf '%s\n' "$text" | sed -e 's@/\*[^*]*\*/@@g' -e 's@//.*@@')
    case "$code" in
        *moo_numbridge*) ;;
        *) continue ;;
    esac
    if [ "$file" = "Common/include/moo_number.h" ]; then
        case "$code" in
            *inline*moo_numbridge*) printf '%s\tdefinition\tok\t%s\n' "$file" "$line" >>"$all"; continue ;;
        esac
    fi

    allowed=0
    why=
    case "$file" in
        VM/src/lmathlib.cpp)
            allowed=1
            why="math library"
            ;;
        VM/src/lgcdebug.cpp)
            allowed=1
            why="debug output"
            ;;
        VM/src/lnumutils.h)
            case "$code" in
                *luai_numpow*) allowed=1; why="luai_numpow" ;;
            esac
            ;;
        VM/src/lbuiltins.cpp)
            fn=$(func_at "$file" "$line")
            case "$fn" in
                luauF_*)
                    case "$fn" in
                        *vector*) ;;
                        *) allowed=1; why="math fastcall $fn" ;;
                    esac
                    ;;
            esac
            ;;
    esac
    if [ "$allowed" -eq 1 ]; then
        printf '%s\tcall\tok\t%s:%s: %s\n' "$file" "$file" "$line" "$why" >>"$all"
    else
        printf '%s\tcall\tbad\t%s:%s: bridge call outside the allowlist\n' "$file" "$file" "$line" >>"$all"
    fi
done <"$calls"

# a marker beside an allowlisted bridge line is allowed with it
while IFS= read -r rec; do
    [ -n "$rec" ] || continue
    file=${rec%%:*}
    rest=${rec#*:}
    line=${rest%%:*}
    text=${rest#*:}
    in_build "$file" || continue
    next=$(sed -n "$((line + 1))p" "$root/$file" 2>/dev/null || true)
    nearby=$(sed -n "${line},$((line + 4))p" "$root/$file" 2>/dev/null || true)

    allowed=0
    why=
    case "$file" in
        VM/src/lmathlib.cpp)
            allowed=1
            why="math library"
            ;;
        VM/src/lgcdebug.cpp)
            allowed=1
            why="debug output"
            ;;
        VM/src/lnumutils.h)
            case "$text$next" in
                *luai_numpow*|*pow*) allowed=1; why="luai_numpow" ;;
            esac
            ;;
        VM/src/lbuiltins.cpp)
            fn=$(func_at "$file" "$line")
            case "$fn" in
                luauF_*)
                    case "$fn" in
                        *vector*) ;;
                        *) allowed=1; why="math fastcall $fn" ;;
                    esac
                    ;;
            esac
            ;;
        Common/include/moo_number.h)
            case "$nearby" in
                *moo_numbridge*|*BRIDGE*) allowed=1; why="bridge definitions" ;;
            esac
            ;;
    esac
    if [ "$allowed" -eq 1 ]; then
        printf '%s\tmarker\tok\t%s:%s: %s\n' "$file" "$file" "$line" "$why" >>"$all"
    else
        printf '%s\tmarker\tbad\t%s:%s: TEMPORARY marker outside the allowlist\n' "$file" "$file" "$line" >>"$all"
    fi
done <"$marks"

total=$(awk -F '\t' '{ n++ } END { print n + 0 }' "$all")
bad=$(awk -F '\t' '$3 == "bad" { n++ } END { print n + 0 }' "$all")
calls_n=$(awk -F '\t' '$2 == "call" { n++ } END { print n + 0 }' "$all")
marks_n=$(awk -F '\t' '$2 == "marker" { n++ } END { print n + 0 }' "$all")
defs_n=$(awk -F '\t' '$2 == "definition" { n++ } END { print n + 0 }' "$all")

echo "moo-check-bridges: $calls_n bridge calls, $marks_n TEMPORARY markers, $defs_n definitions, $bad outside the allowlist"
if [ "$total" -ne 0 ]; then
    echo "by file (calls, markers, definitions):"
    awk -F '\t' '
        {
            files[$1] = 1
            if ($2 == "call") calls[$1]++
            else if ($2 == "marker") marks[$1]++
            else if ($2 == "definition") defs[$1]++
            if ($3 == "bad") bad[$1]++
        }
        END {
            for (file in files)
                printf "%4d %4d %4d  %s%s\n", calls[file] + 0, marks[file] + 0, defs[file] + 0, file, (bad[file] ? "  FINDING" : "")
        }
    ' "$all" | LC_ALL=C sort -k4
fi

if [ "$bad" -ne 0 ]; then
    echo "findings:"
    awk -F '\t' '$3 == "bad" { print "  " $4 }' "$all" | LC_ALL=C sort
    exit 1
fi

echo "moo-check-bridges: every bridge call and TEMPORARY marker is in the allowlist"
