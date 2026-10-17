#!/bin/sh
# moo: the VM reads no clock, no environment and no C random generator (docs/DETERMINISM.md, 4, 9, 10).
# Checked at the link level: no object in the VM library but lperf.cpp (which defines lua_clock for
# hosts) may reference one of these functions.
#
# usage: tools/moo-check-no-clock.sh build/libLuau.VM.a
set -eu
lib="$1"
test -f "$lib"
banned='(lua_clock|clock|clock_gettime|time|gettimeofday|mach_absolute_time|mach_continuous_time|QueryPerformanceCounter|getenv|rand|srand|random)'
found=$(nm -A -C "$lib" 2>/dev/null | grep -v 'lperf\.cpp\.o' | grep -E " U _?${banned}(\(\))?\$" || true)
if [ -n "$found" ]; then
    echo "moo-check-no-clock: the VM references a clock, the environment or a C random generator:"
    echo "$found"
    exit 1
fi
echo "moo-check-no-clock: $lib references no clock"
