/* SPDX-License-Identifier: Apache-2.0 */
#include "bench.h"

int main(void)
{
    uint32_t checksum = 0;

    if (!bench_run(&checksum) || checksum != bench_expected) {
        printk("RV32EMU_BENCH_FAIL case=%s phase=warmup\n", BENCH_CASE);
        return 1;
    }
    printk("RV32EMU_BENCH_WARMUP case=%s iterations=%u checksum=%u\n",
           BENCH_CASE, bench_iterations, checksum);
    printk("RV32EMU_BENCH_READY case=%s\n", BENCH_CASE);
    printk("RV32EMU_BENCH_START case=%s iterations=%u\n", BENCH_CASE,
           bench_iterations);
    checksum = 0;
    bool valid = bench_run(&checksum);

    printk("RV32EMU_BENCH_END case=%s iterations=%u checksum=%u status=%s\n",
           BENCH_CASE, bench_iterations, checksum,
           valid && checksum == bench_expected ? "PASS" : "FAIL");
    return valid && checksum == bench_expected ? 0 : 1;
}
