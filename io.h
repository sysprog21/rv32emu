#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct {
    uint8_t data[0x10000];
} chunk_t;

typedef struct {
    chunk_t *chunks[0x10000];
} memory_t;

memory_t *memory_new();
void memory_delete(memory_t *m);

uint32_t memory_read_str(memory_t *m,
                         uint8_t *dst,
                         uint32_t addr,
                         uint32_t max);
uint32_t memory_read_ifetch(memory_t *m, uint32_t addr);
uint32_t memory_read_w(memory_t *m, uint32_t addr);
uint16_t memory_read_s(memory_t *m, uint32_t addr);
uint8_t memory_read_b(memory_t *m, uint32_t addr);
void memory_read(memory_t *m, uint8_t *dst, uint32_t addr, uint32_t size);
void memory_write(memory_t *m,
                  uint32_t addr,
                  const uint8_t *src,
                  uint32_t size);
void memory_fill(memory_t *m, uint32_t addr, uint32_t size, uint8_t val);

#ifdef __cplusplus
};  // ifdef __cplusplus
#endif
