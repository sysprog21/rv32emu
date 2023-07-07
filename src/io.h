/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <stdint.h>

/* Directly map a memory with a size of 2^32 bytes. All memory read/write
 * operations can access this memory through the memory subsystem.
 */

typedef struct {
    uint8_t *mem_base;
    uint64_t mem_size;
} memory_t;

memory_t *memory_new();
void memory_delete(memory_t *m);

/* read a C-style string from memory */
uint32_t memory_read_str(memory_t *m,
                         uint8_t *dst,
                         uint32_t addr,
                         uint32_t max);

/* read an instruction from memory */
uint32_t memory_ifetch(uint32_t addr);

/* read a word from memory */
uint32_t memory_read_w(uint32_t addr);

/* read a short from memory */
uint16_t memory_read_s(uint32_t addr);

/* read a byte from memory */
uint8_t memory_read_b(uint32_t addr);

/* read a length of data from memory */
void memory_read(memory_t *m, uint8_t *dst, uint32_t addr, uint32_t size);

void memory_write(memory_t *m,
                  uint32_t addr,
                  const uint8_t *src,
                  uint32_t size);

void memory_write_w(uint32_t addr, const uint8_t *src);

void memory_write_s(uint32_t addr, const uint8_t *src);

void memory_write_b(uint32_t addr, const uint8_t *src);

void memory_fill(memory_t *m, uint32_t addr, uint32_t size, uint8_t val);
