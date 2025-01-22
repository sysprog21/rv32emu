/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

/* enable/disable (compile time) features in this header */

/* Standard Extension for Integer Multiplication and Division */
#ifndef RV32_FEATURE_EXT_M
#define RV32_FEATURE_EXT_M 1
#endif

/* Standard Extension for Atomic Instructions */
#ifndef RV32_FEATURE_EXT_A
#define RV32_FEATURE_EXT_A 1
#endif

/* Standard Extension for Single-Precision Floating Point Instructions */
#ifndef RV32_FEATURE_EXT_F
#define RV32_FEATURE_EXT_F 1
#endif

/* Standard Extension for Compressed Instructions */
#ifndef RV32_FEATURE_EXT_C
#define RV32_FEATURE_EXT_C 1
#endif

/* RV32E Base Integer Instruction Set */
#ifndef RV32_FEATURE_RV32E
#define RV32_FEATURE_RV32E 0
#endif

/* Control and Status Register (CSR) */
#ifndef RV32_FEATURE_Zicsr
#define RV32_FEATURE_Zicsr 1
#endif

/* Instruction-Fetch Fence */
#ifndef RV32_FEATURE_Zifencei
#define RV32_FEATURE_Zifencei 1
#endif

/* Zba Address generation instructions */
#ifndef RV32_FEATURE_Zba
#define RV32_FEATURE_Zba 1
#endif

/* Zbb Basic bit-manipulation */
#ifndef RV32_FEATURE_Zbb
#define RV32_FEATURE_Zbb 1
#endif

/* Zbc Carry-less multiplication */
#ifndef RV32_FEATURE_Zbc
#define RV32_FEATURE_Zbc 1
#endif

/* Zbs Single-bit instructions */
#ifndef RV32_FEATURE_Zbs
#define RV32_FEATURE_Zbs 1
#endif

/* Experimental SDL oriented system calls */
#ifndef RV32_FEATURE_SDL
#define RV32_FEATURE_SDL 1
#endif

/* GDB remote debugging */
#ifndef RV32_FEATURE_GDBSTUB
#define RV32_FEATURE_GDBSTUB 0
#endif

/* Experimental just-in-time compiler */
#ifndef RV32_FEATURE_JIT
#define RV32_FEATURE_JIT 0
#endif

/* Experimental tier-2 just-in-time compiler */
#ifndef RV32_FEATURE_T2C
#define RV32_FEATURE_T2C 0
#endif

/* T2C depends on JIT configuration */
#if !RV32_FEATURE_JIT
#undef RV32_FEATURE_T2C
#define RV32_FEATURE_T2C 0
#endif

/* System */
#ifndef RV32_FEATURE_SYSTEM
#define RV32_FEATURE_SYSTEM 0
#endif

/* Use ELF loader */
#ifndef RV32_FEATURE_ELF_LOADER
#define RV32_FEATURE_ELF_LOADER 0
#endif

/* MOP fusion */
#ifndef RV32_FEATURE_MOP_FUSION
#define RV32_FEATURE_MOP_FUSION 1
#endif

/* Block chaining */
#ifndef RV32_FEATURE_BLOCK_CHAINING
#define RV32_FEATURE_BLOCK_CHAINING 1
#endif

/* Feature test macro */
#define RV32_HAS(x) RV32_FEATURE_##x
