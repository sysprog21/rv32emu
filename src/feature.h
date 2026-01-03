/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

/* Feature Configuration
 *
 * Features are controlled via Kconfig (configs/Kconfig) and passed as
 * compiler flags (-DRV32_FEATURE_*). The defaults below are used only
 * if the feature flag is not set by the build system.
 *
 * Kconfig Constraints (invalid combinations are prevented at config time):
 *   - T2C requires JIT && LLVM18 (enforced below and in Kconfig)
 *   - JIT is incompatible with Emscripten (WASM uses interpreter only)
 *   - GDBSTUB is incompatible with Emscripten
 *   - SDL requires SDL2 library or Emscripten
 *   - SDL_MIXER requires SDL
 *   - ELF_LOADER requires SYSTEM mode
 *
 * Derived Features (computed from other features):
 *   - SYSTEM_MMIO = SYSTEM && !ELF_LOADER (for kernel boot with MMIO devices)
 *
 * Simplification Rules (Kconfig guarantees these):
 *   - RV32_HAS(T2C) implies RV32_HAS(JIT) - no need to check both
 *   - RV32_HAS(ELF_LOADER) implies RV32_HAS(SYSTEM)
 *   - RV32_HAS(SDL_MIXER) implies RV32_HAS(SDL)
 *   - Use RV32_HAS(SYSTEM_MMIO) instead of RV32_HAS(SYSTEM) &&
 *     !RV32_HAS(ELF_LOADER)
 */

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

/* T2C (tier-2 compiler) requires JIT (tier-1 compiler).
 * Kconfig also enforces this, but we double-check here for safety.
 * This allows source code to use just RV32_HAS(T2C) without also checking JIT.
 */
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

/* Logging with color */
#ifndef RV32_FEATURE_LOG_COLOR
#define RV32_FEATURE_LOG_COLOR 1
#endif

/* Architecture test */
#ifndef RV32_FEATURE_ARCH_TEST
#define RV32_FEATURE_ARCH_TEST 0
#endif

/* MMIO support for system emulation
 * It is enabled when running in SYSTEM mode without ELF_LOADER, corresponding
 * to booting a full Linux kernel that requires memory-mapped I/O to interact
 * with virtual devices (UART, PLIC, virtio-blk).
 */
#if RV32_FEATURE_SYSTEM && !RV32_FEATURE_ELF_LOADER
#define RV32_FEATURE_SYSTEM_MMIO 1
#else
#define RV32_FEATURE_SYSTEM_MMIO 0
#endif

/* Feature test macro */
#define RV32_HAS(x) RV32_FEATURE_##x
