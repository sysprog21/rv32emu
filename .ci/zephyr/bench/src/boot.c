/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 * SPDX-License-Identifier: Apache-2.0
 */
/* Zephyr samples/hello_world/src/main.c at bbc6385f0a2 (Apache-2.0). */
#include "bench.h"

const uint32_t bench_iterations = 1;
const uint32_t bench_expected = 1;
bool bench_run(uint32_t *checksum)
{
    printk("Hello World! %s\n", CONFIG_BOARD_TARGET);
    *checksum = 1;
    return true;
}
