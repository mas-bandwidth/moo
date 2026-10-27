#!/bin/sh
# moo: CI's compiler cache is an accelerator, never a gate. This probe starts the sccache server, whose startup
# checks the GitHub Actions cache, and writes the launcher for cmake to $GITHUB_OUTPUT: "sccache" when the server
# is up, empty when sccache is missing or the cache is out (an HTTP 503 "Egress is over the account limit" fails
# the startup), so the job builds without it. A compile or test failure is never touched: this sets only
# CMAKE_<LANG>_COMPILER_LAUNCHER, and SCCACHE_IGNORE_SERVER_IO_ERROR=1 (ci.yml) makes a server lost mid-build
# fall back to the compiler.
#
# usage: tools/moo-ci-sccache.sh   (in a GitHub Actions step with id, reading steps.<id>.outputs.launcher)

set -u

out=${GITHUB_OUTPUT:-/dev/stdout}

if command -v sccache >/dev/null 2>&1 && sccache --start-server; then
    echo "moo-ci-sccache: the cache is up"
    echo "launcher=sccache" >>"$out"
else
    echo "::warning::sccache or its cache is unavailable; this job builds without the cache"
    echo "launcher=" >>"$out"
fi
exit 0
