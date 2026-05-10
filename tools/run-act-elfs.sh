#!/usr/bin/env sh
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <rv32emu-user> <act-elf-dir>" >&2
    exit 2
fi

emu=$1
elf_dir=$2

if [ ! -x "$emu" ]; then
    echo "ACT runner: emulator is not executable: $emu" >&2
    exit 2
fi

if [ ! -d "$elf_dir" ]; then
    echo "ACT runner: ELF directory does not exist: $elf_dir" >&2
    exit 2
fi

total=0
failed=0

find "$elf_dir" -type f -name '*.elf' | sort | while IFS= read -r elf; do
    total=$((total + 1))
    sig=$(mktemp)
    if "$emu" -q -a "$sig" "$elf"; then
        printf 'ACT %-72s [OK]\n' "${elf#$elf_dir/}"
    else
        failed=$((failed + 1))
        printf 'ACT %-72s [FAIL]\n' "${elf#$elf_dir/}"
    fi
    rm -f "$sig"

    echo "$total $failed" > "${TMPDIR:-/tmp}/rv32emu-act-counts.$$"
done

if [ -f "${TMPDIR:-/tmp}/rv32emu-act-counts.$$" ]; then
    set -- $(cat "${TMPDIR:-/tmp}/rv32emu-act-counts.$$")
    total=$1
    failed=$2
    rm -f "${TMPDIR:-/tmp}/rv32emu-act-counts.$$"
fi

if [ "$total" -eq 0 ]; then
    echo "ACT runner: no ELF files found in $elf_dir" >&2
    exit 2
fi

if [ "$failed" -ne 0 ]; then
    echo "ACT runner: $failed/$total failed" >&2
    exit 1
fi

echo "ACT runner: $total/$total passed"
