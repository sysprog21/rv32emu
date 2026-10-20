/*
 * Copyright (c) 2023 Basalte bv
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Adapted from Zephyr bbc6385f0a2 tests/modules/nanopb/src/main.c.
 * Retain the simple-message roundtrip and check every decoded byte.
 */
#include <pb_decode.h>
#include <pb_encode.h>
#include <proto/simple.pb.h>
#include <string.h>
#include "bench.h"
const uint32_t bench_iterations = WORK_ITERATIONS;
const uint32_t bench_expected = WORK_ITERATIONS * 28U;
bool bench_run(uint32_t *checksum)
{
    for (unsigned n = 0; n < WORK_ITERATIONS; ++n) {
        uint8_t buffer[SimpleMessage_size];
        SimpleMessage msg = SimpleMessage_init_zero;

        for (size_t i = 0; i < sizeof(msg.buffer); ++i) {
            msg.buffer[i] = i;
        }
        pb_ostream_t out = pb_ostream_from_buffer(buffer, sizeof(buffer));

        if ((!pb_encode(&out, SimpleMessage_fields, &msg)) ||
            (out.bytes_written != 10) || (buffer[0] != 0x0a) ||
            (buffer[1] != 8) || (memcmp(buffer + 2, msg.buffer, 8) != 0)) {
            return false;
        }
        memset(&msg, 0, sizeof(msg));
        pb_istream_t in = pb_istream_from_buffer(buffer, out.bytes_written);

        if ((!pb_decode(&in, SimpleMessage_fields, &msg)) ||
            (in.bytes_left != 0)) {
            return false;
        }
        for (size_t i = 0; i < sizeof(msg.buffer); ++i) {
            if (msg.buffer[i] != i) {
                return false;
            }
            *checksum += msg.buffer[i];
        }
        pb_release(SimpleMessage_fields, &msg);
    }
    return true;
}
