#!/usr/bin/env python3

import os


def list_files(d, ignore_list=None):
    if ignore_list is None:
        ignore_list = []
    try:
        if d == "build":
            files = [
                f
                for f in os.listdir(d)
                if os.path.isfile(os.path.join(d, f))
                and f.endswith(".elf")
                and not any(
                    f.endswith(ign) or f.startswith(ign) for ign in ignore_list
                )
            ]
        else:
            parent_dir = os.path.dirname(d)
            files = [
                os.path.relpath(os.path.join(d, f), start=parent_dir)
                for f in os.listdir(d)
                if os.path.isfile(os.path.join(d, f))
                and not any(
                    f.endswith(ign) or os.path.join(d, f).endswith(ign)
                    for ign in ignore_list
                )
            ]
        return files
    except FileNotFoundError:
        print(f"Directory {d} not found.")
        return []


elf_exec_dirs = ["build", "build/riscv32"]
msg_less_ignore_files = [
    "cc.elf",
    "chacha20.elf",
    "riscv32/lena",
    "riscv32/puzzle",
    "riscv32/line",
    "riscv32/captcha",
]  # List of files to ignore
elf_exec_list = []

for d in elf_exec_dirs:
    files = list_files(d, ignore_list=msg_less_ignore_files)
    elf_exec_list.extend(files)


def gen_elf_list_js():
    js_code = f"const elfFiles = {elf_exec_list};\n"
    print(js_code)


gen_elf_list_js()
