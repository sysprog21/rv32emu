#!/usr/bin/env python3

import json
import subprocess

def run_benchmark(b):
    interp = None
    if "sh" in b:
        interp = "bash"
    elif "py" in b:
        interp = "python3"

    subprocess.run(args=[interp, b], shell=False, check=True)

def load_benchmark(file):
    f = open(file, "r")
    return json.load(f)

# run benchmarks
benchmarks = [
    "tests/dhrystone.sh",
    "tests/coremark.py"
]
for b in benchmarks:
    run_benchmark(b)

# combine benchmarks output data
benchmarks_output = [
    "dhrystone_output.json",
    "coremark_output.json"
]
benchmark_data = [load_benchmark(bo) for bo in benchmarks_output]

benchmark_output = "benchmark_output.json"
f = open(benchmark_output, "w")
f.write(json.dumps(benchmark_data, indent=4))
f.close()
