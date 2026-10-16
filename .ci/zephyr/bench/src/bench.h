/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define WORK_ITERATIONS 1000U
bool bench_run(uint32_t *checksum);
extern const uint32_t bench_iterations;
extern const uint32_t bench_expected;
