#!/bin/sh
# moo: model-check every case in tla/CASES.tsv with TLC and compare with the expected result.
# A "pass" case must find no error; an "invariant" case (a plant) must violate the named invariant.
#
# usage: tools/moo-tlc.sh path/to/tla2tools.jar
set -eu
jar="$1"
cd "$(dirname "$0")/../tla"
tail -n +2 CASES.tsv | while IFS="$(printf '\t')" read -r config module expected property; do
    out=$(java -XX:+UseParallelGC -cp "$jar" tlc2.TLC -workers auto -config "$config" "$module" 2>&1 || true)
    rm -rf states
    case "$expected" in
    pass)
        if echo "$out" | grep -q "No error has been found"; then result=ok; else result=FAIL; fi ;;
    invariant)
        if echo "$out" | grep -q "Invariant $property is violated"; then result=ok; else result=FAIL; fi ;;
    *)
        result=FAIL ;;
    esac
    generated=$(echo "$out" | grep -E "states generated" | tail -1)
    echo "$result $config ($expected $property): $generated"
    if [ "$result" != ok ]; then
        echo "$out" | tail -30
        exit 1
    fi
done
