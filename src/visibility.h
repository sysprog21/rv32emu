/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */
#pragma once

#include "feature.h"

/* The optional collector crosses a DSO boundary. These declarations are not
 * public embedding APIs; normal native builds keep them hidden too.
 */
#if RV32_HAS(T2C) && (defined(__GNUC__) || defined(__clang__))
#define RV_T2C_EXPORT __attribute__((visibility("default")))
#else
#define RV_T2C_EXPORT
#endif
