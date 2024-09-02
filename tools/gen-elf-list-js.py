#!/usr/bin/env python3

import os

def list_files(d):
    try:
        if d == "build":
            files = [f for f in os.listdir(d) if (os.path.isfile(os.path.join(d, f)) and f.endswith('.elf'))]
        else:
            parent_dir = os.path.dirname(d)
            files = [
                os.path.relpath(os.path.join(d, f), start=parent_dir)
                for f in os.listdir(d)
                if os.path.isfile(os.path.join(d, f))
            ]
        return files
    except FileNotFoundError:
        print(f"Directory {directory} not found.")
        return []

elf_exec_dirs = ["build", "build/riscv32"]
elf_exec_list = []

for d in elf_exec_dirs:
    files = list_files(d)
    elf_exec_list.extend(files)
#print(elf_exec_list)

def gen_elf_list_js():
    js_code = f"const elfFiles = {elf_exec_list};\n"
    print(js_code)

gen_elf_list_js()
