#!/usr/bin/env bash
set -euo pipefail

# Generate one self-checking suite and reuse it across execution modes.
# setup-build supplies GCC 15+, uv, Ruby/UDB, Sail, and the pinned LLVM.
export PATH="$PWD/toolchain/bin:$PATH"
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN)}
make cleanconfig
make ci_defconfig
make arch-test-elfs MISE= JOBS="$jobs"
make check-arch-test-runner

elf_dir=build/arch-test/rv32emu-rv32imafc/elfs
mkdir -p build/arch-test/results
git -C tests/riscv-arch-test rev-parse HEAD > build/arch-test/results/act4-revision.txt

# A failed LLVM detection only warns and drops T2C, which would run the
# tiered mode as tier-1 alone, so require the object it builds.
t2c_built()
{
    [ "$1" = 0 ] || test -f "build/arch-test-runners/$2/t2c.o"
}

failed=()
# mode:JIT:T2C:fusion, where fusion also selects block chaining
for spec in interpreter:0:0:0 interpreter-fused:0:0:1 tier1:1:0:0 \
    tier1-fused:1:0:1 tiered:1:1:1; do
    IFS=: read -r mode jit t2c fused <<< "$spec"
    options=(CC="${CC:-cc}" OUT="build/arch-test-runners/$mode"
        ENABLE_JIT="$jit" ENABLE_T2C="$t2c"
        ENABLE_MOP_FUSION="$fused" ENABLE_BLOCK_CHAINING="$fused")
    printf '\nACT4 execution mode: %s\n' "$mode"
    # Keep going so one failing mode does not hide the others' results, and
    # log build failures too so the uploaded results show why a mode failed.
    {
        make -j"$jobs" "${options[@]}" check-arch-test-tohost check-arch-test-misalign \
            && t2c_built "$t2c" "$mode" \
            && tools/run-arch-test-elfs.sh "build/arch-test-runners/$mode/rv32emu" "$elf_dir"
    } 2>&1 | tee "build/arch-test/results/$mode.log" || failed+=("$mode")
done

if [ "${#failed[@]}" -ne 0 ]; then
    echo "ACT4 failed in: ${failed[*]}" >&2
    exit 1
fi
