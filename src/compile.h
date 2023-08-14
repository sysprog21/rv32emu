/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once
#include <stdint.h>

#include "riscv.h"

uint8_t *block_compile(riscv_t *rv);
