#!/usr/bin/env python3
import subprocess
import re
import numpy
import os
import json

iter = 1
coremark_param = "0x0 0x0 0x66 30000 7 1 2000"
res = []
file_exist = os.path.exists("build/rv32emu")
if not file_exist:
    print("Please compile before running test")
    exit(1)
print("Start Test CoreMark benchmark")
comp_proc = subprocess.check_output(
    "build/rv32emu build/riscv32/coremark {}".format(coremark_param), shell=True
).decode("utf-8")
if not comp_proc or comp_proc.find("Error") != -1:
    print("Test Error")
    exit(1)
else:
    print("Test Pass")

for i in range(iter):
    print("Running CoreMark benchmark - Run #{}".format(i + 1))
    comp_proc = subprocess.check_output(
        "build/rv32emu build/riscv32/coremark {}".format(coremark_param),
        shell=True,
    ).decode("utf-8")
    if not comp_proc:
        print("Fail\n")
        exit(1)
    else:
        res.append(
            float(
                re.findall(r"Iterations/Sec   : [0-9]+.[0-9]+", comp_proc)[0][
                    19:
                ]
            )
        )

mean = numpy.mean(res, dtype=numpy.float64)
deviation = numpy.std(res, dtype=numpy.float64)
for n in res:
    if abs(n - mean) > (deviation * 2):
        res.remove(n)

print("{:.3f}".format(numpy.mean(res, dtype=numpy.float64)))

# save Average Iterations/Sec in JSON format for benchmark action workflow
benchmark_output = "coremark_output.json"
benchmark_data = {
    "name": "Coremark",
    "unit": "Average iterations/sec over 10 runs",
    "value": float("{:.3f}".format(numpy.mean(res, dtype=numpy.float64))),
}
f = open(benchmark_output, "w")
f.write(json.dumps(benchmark_data))
f.close()
