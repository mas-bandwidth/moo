#!/usr/bin/env python3
# This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
"""moo: the compiler is deterministic across processes (docs/DETERMINISM.md, 25).

Runs moo-compile --binary once per pin in tests/determinism/bytecode.txt, in separate processes, each from a
different working directory, locale and path spelling (absolute, or relative to that directory), and requires every
output to hash to its pin: the same FNV-1a 64 that tests/moo/compile_determinism.test.cpp computes in process.
Each pin runs in --variants variants (default 2).

A functional test: it starts processes and uses the file system. The environment passes through, so the nightly
runs it under MALLOC_PERTURB_ (glibc) and MallocScribble (macOS), and --wrap runs each compile under a tool such
as Valgrind memcheck (an exit status of --wrap-error marks a memory error, not a compile error).

usage: tools/moo-check-compile.py <moo-compile> [--root <checkout>] [--pins <file>] [--variants N] [--jobs N]
                                  [--wrap "<command>"] [--wrap-error N]
  --pins defaults to tests/determinism/bytecode.txt; a checked build (MOO_OVERFLOW_ERRORS=ON) passes
  tests/determinism/bytecode-checked.txt, since its compiler leaves a saturating fold to the VM.
Exit 0 when every output matches its pin; 1 otherwise, naming each mismatch.
"""

import argparse
import concurrent.futures
import os
import shlex
import subprocess
import sys
import tempfile

LOCALES = ["C", "C.UTF-8", "en_US.UTF-8", "de_DE.UTF-8", "tr_TR.UTF-8", "fr_FR.UTF-8", "ja_JP.UTF-8"]


def fnv1a(data):
    h = 0xCBF29CE484222325
    for c in data:
        h = ((h ^ c) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % h


def read_pins(path):
    pins = []
    with open(path, "rb") as f:
        for raw in f.read().decode("utf-8").split("\n"):
            if not raw or raw.startswith("#"):
                continue
            name, o, g, pin = raw.split(" ")
            pins.append((name, int(o[1:]), int(g[1:]), pin))
    return pins


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("compiler")
    ap.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap.add_argument("--pins", default="")
    ap.add_argument("--variants", type=int, default=2)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--wrap", default="")
    ap.add_argument("--wrap-error", type=int, default=99)
    args = ap.parse_args()

    compiler = os.path.abspath(args.compiler)
    tests = os.path.join(os.path.abspath(args.root), "tests")
    pins = read_pins(args.pins or os.path.join(tests, "determinism", "bytecode.txt"))
    wrap = shlex.split(args.wrap)

    with tempfile.TemporaryDirectory(prefix="moo check compile ") as scratch:
        cwds = [scratch, os.path.join(scratch, "déjà vu"), os.path.abspath(args.root), tests, os.path.join(tests, "conformance")]
        os.makedirs(cwds[1], exist_ok=True)

        def run(job):
            index, variant, (name, o, g, pin) = job
            k = index * 7 + variant * 3
            cwd = cwds[k % len(cwds)]
            locale = LOCALES[(k // len(cwds)) % len(LOCALES)]
            path = os.path.join(tests, *name.split("/"))
            if variant % 2 == 1:
                try:
                    path = os.path.relpath(path, cwd)
                except ValueError:  # another drive on Windows
                    pass
            env = dict(os.environ, LC_ALL=locale, LANG=locale)
            proc = subprocess.run(wrap + [compiler, "--binary", "-O%d" % o, "-g%d" % g, path], cwd=cwd, env=env, capture_output=True)
            where = "%s -O%d -g%d (cwd %s, LC_ALL=%s, path %s)" % (name, o, g, cwd, locale, path)
            if wrap and proc.returncode == args.wrap_error:
                return "%s: memory error\n%s" % (where, proc.stderr.decode("utf-8", "replace")[-4000:])
            got = "error" if proc.returncode != 0 else fnv1a(proc.stdout)
            if got != pin:
                return "%s: %s, pinned %s" % (where, got, pin)
            return None

        jobs = [(i, v, p) for i, p in enumerate(pins) for v in range(args.variants)]
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            failures = [r for r in pool.map(run, jobs) if r]

    for failure in failures[:40]:
        print("moo-check-compile: " + failure)
    print("moo-check-compile: %d compiles of %d pins in separate processes, %d differ" % (len(jobs), len(pins), len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
