/*
 * Copyright 2018 Embedded Microprocessor Benchmark Consortium (EEMBC)
 * SPDX-License-Identifier: Apache-2.0
 */
/* Initialization follows core_main.c at 1f483d5b8316; all three original
 * algorithms run through iterate(), with the upstream 2K CRC checks.
 * Host fixed-work timing does not produce a reportable CoreMark score.
 */
#include <string.h>
#include "bench.h"
#include "coremark.h"
static uint8_t memory[TOTAL_DATA_SIZE] __aligned(8);
volatile ee_s32 seed1_volatile = 0;
volatile ee_s32 seed2_volatile = 0;
volatile ee_s32 seed3_volatile = 0x66;
volatile ee_s32 seed4_volatile = ITERATIONS;
volatile ee_s32 seed5_volatile = 0;
ee_u32 default_num_contexts = 1;
const uint32_t bench_iterations = ITERATIONS;
const uint32_t bench_expected = 0xe714U + 0x1fd7U + 0x8e3aU + 0x988cU;
bool bench_run(uint32_t *checksum)
{
    core_results result = {0};

    memset(memory, 0, sizeof(memory));
    result.seed1 = seed1_volatile;
    result.seed2 = seed2_volatile;
    result.seed3 = seed3_volatile;
    result.iterations = ITERATIONS;
    result.execs = ALL_ALGORITHMS_MASK;
    result.size = TOTAL_DATA_SIZE / 3;
    result.memblock[0] = memory;
    for (unsigned i = 0; i < 3; ++i) {
        result.memblock[i + 1] = memory + result.size * i;
    }
    result.list = core_list_init(result.size, result.memblock[1], result.seed1);
    core_init_matrix(result.size, result.memblock[2],
                     (ee_s32) result.seed1 | ((ee_s32) result.seed2 << 16),
                     &result.mat);
    core_init_state(result.size, result.seed1, result.memblock[3]);
    iterate(&result);
    *checksum =
        result.crclist + result.crcmatrix + result.crcstate + result.crc;
    return result.crclist == 0xe714 && result.crcmatrix == 0x1fd7 &&
           result.crcstate == 0x8e3a && result.crc == 0x988c &&
           check_data_types() == 0;
}
