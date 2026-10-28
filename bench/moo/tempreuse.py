#!/usr/bin/env python3
# This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
"""moo: the expression-temporary reuse benchmark (docs/DESIGN.md "Expression-temporary reuse").

Runs each program in bench/moo/tempreuse/ with every moo binary given, each RUNS times (default 7), and prints the
objects per iteration the program counts and the median wall time. The objects are exact (creation numbers); the
time is the host's clock around the whole process, so it is an indication on the machine it ran on, not a promise.

usage: bench/moo/tempreuse.py [--runs N] label=path/to/moo[:-O<n>] ...
"""
import argparse
import os
import statistics
import subprocess
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PROGRAMS = ["expr", "particles", "rotate", "number"]


def run(binary, flags, program):
    start = time.perf_counter()
    out = subprocess.run([binary] + flags + [os.path.join(HERE, "tempreuse", program + ".luau")], check=True,
                         capture_output=True, text=True).stdout
    return time.perf_counter() - start, out.split()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--runs", type=int, default=7)
    parser.add_argument("binaries", nargs="+")
    args = parser.parse_args()

    configs = []
    for spec in args.binaries:
        label, _, rest = spec.partition("=")
        path, _, flag = rest.partition(":")
        configs.append((label, path, [flag] if flag else []))

    print("| program | " + " | ".join(f"{label} objects/iter | {label} s" for label, _, _ in configs) + " |")
    print("|---|" + "---|---|" * len(configs))
    for program in PROGRAMS:
        cells = []
        checksums = set()
        for label, path, flags in configs:
            times = []
            for _ in range(args.runs):
                elapsed, fields = run(path, flags, program)
                times.append(elapsed)
            objects = round(float(fields[1]), 3)
            checksums.add(tuple(fields[2:]))
            cells.append(f"{objects:g} | {statistics.median(times):.3f}")
        same = "" if len(checksums) == 1 else " (checksums differ!)"
        print(f"| {program}{same} | " + " | ".join(cells) + " |")


if __name__ == "__main__":
    main()
