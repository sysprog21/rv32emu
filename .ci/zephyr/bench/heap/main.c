/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Keep tests/lib/heap/src/main.c at Zephyr bbc6385f0a2 unchanged, including
 * its stress and fragmentation cases. One work unit is the whole suite.
 */
#include <zephyr/ztest.h>
static unsigned passed(void)
{
    unsigned count = 0;
    struct ztest_unit_test *test;

    for (test = _ztest_unit_test_list_start; test < _ztest_unit_test_list_end;
         ++test) {
        if ((test->stats->fail_count) || (test->stats->skip_count)) {
            return 0;
        }
        count += test->stats->pass_count;
    }
    return count;
}
void test_main(void)
{
    ztest_run_all(NULL, false, 1, 1);
    ztest_verify_all_test_suites_ran();
    if (passed() != 7) {
        printk("RV32EMU_BENCH_FAIL case=heap-regression phase=warmup\n");
        return;
    }
    printk(
        "RV32EMU_BENCH_WARMUP case=heap-regression iterations=1 checksum=7\n");
    printk("RV32EMU_BENCH_READY case=heap-regression\n");
    printk("RV32EMU_BENCH_START case=heap-regression iterations=1\n");
    ztest_run_all(NULL, false, 1, 1);
    ztest_verify_all_test_suites_ran();
    unsigned count = passed();

    printk(
        "RV32EMU_BENCH_END case=heap-regression iterations=1 checksum=%u "
        "status=%s\n",
        count == 14 ? 7 : 0, count == 14 ? "PASS" : "FAIL");
}
