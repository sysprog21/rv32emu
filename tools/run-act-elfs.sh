#!/usr/bin/env sh
set -eu

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
    echo "usage: $0 <rv32emu-user> <act-elf-dir> [extensions]" >&2
    exit 2
fi

emu=$1
elf_dir=$2
extensions=${3:-}

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
elf_list=$(mktemp)
ext_list=$(mktemp)
trap 'rm -f "$elf_list" "$ext_list"' EXIT

if [ -n "$extensions" ]; then
    printf '%s\n' "$extensions" | tr ',' '\n' > "$ext_list"
    while IFS= read -r ext; do
        [ -n "$ext" ] || continue
        ext_dir=$elf_dir/rv32i/$ext
        if [ ! -d "$ext_dir" ]; then
            echo "ACT runner: extension ELF directory does not exist: $ext_dir" >&2
            exit 2
        fi
        find "$ext_dir" -type f -name '*.elf' >> "$elf_list"
    done < "$ext_list"
    sort -o "$elf_list" "$elf_list"
else
    find "$elf_dir" -type f -name '*.elf' | sort > "$elf_list"
fi

while IFS= read -r elf; do
    total=$((total + 1))
    sig=$(mktemp)
    if "$emu" -q -a "$sig" "$elf"; then
        printf 'ACT %-72s [OK]\n' "${elf#$elf_dir/}"
    else
        failed=$((failed + 1))
        printf 'ACT %-72s [FAIL]\n' "${elf#$elf_dir/}"
    fi
    rm -f "$sig"
done < "$elf_list"

if [ "$total" -eq 0 ]; then
    echo "ACT runner: no ELF files found in $elf_dir" >&2
    exit 2
fi

if [ "$failed" -ne 0 ]; then
    echo "ACT runner: $failed/$total failed" >&2
    exit 1
fi

echo "ACT runner: $total/$total passed"
