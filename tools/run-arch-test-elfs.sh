#!/usr/bin/env sh
set -eu

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
    echo "usage: $0 <emulator> <act-elf-dir> [extensions]" >&2
    exit 2
fi

emu=$1
elf_dir=$2
extensions=${3:-}
test_timeout=${ACT_TIMEOUT:-60}

if ! command -v timeout > /dev/null 2>&1; then
    echo "ACT runner: timeout is required" >&2
    exit 2
fi

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
scratch=$(mktemp -d)
elf_list=$scratch/elfs
trap 'rm -rf "$scratch"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if [ -n "$extensions" ]; then
    : > "$elf_list"
    for ext in $(printf '%s' "$extensions" | tr ',' ' '); do
        selected=$(find "$elf_dir"/*/"$ext" -type f -name '*.elf' 2> /dev/null || :)
        if [ -z "$selected" ]; then
            echo "ACT runner: no ELF files found for extension $ext" >&2
            exit 2
        fi
        printf '%s\n' "$selected" >> "$elf_list"
    done
    sort -u -o "$elf_list" "$elf_list"
else
    find "$elf_dir" -type f -name '*.elf' | sort > "$elf_list"
fi

sig=$scratch/signature
while IFS= read -r elf; do
    total=$((total + 1))
    # Keep the emulator off the ELF list on stdin, or a guest read could
    # consume the remaining names and shrink the run without failing it.
    if timeout --kill-after=5 "$test_timeout" "$emu" -q -a "$sig" "$elf" < /dev/null > "$scratch/output" 2>&1; then
        printf 'ACT %-72s [OK]\n' "${elf#"$elf_dir"/}"
    else
        status=$?
        failed=$((failed + 1))
        printf 'ACT %-72s [FAIL exit=%s]\n' "${elf#"$elf_dir"/}" "$status"
        cat "$scratch/output"
    fi
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
