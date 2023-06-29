/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#ifndef RV32_FEATURES_H
#define RV32_FEATURES_H

/* enable/disable (compile time) features in this header */

/* Standard Extension for Integer Multiplication and Division */
#ifndef RV32_FEATURE_EXT_M
#define RV32_FEATURE_EXT_M 1
#endif

/* Standard Extension for Atomic Instructions */
#ifndef RV32_FEATURE_EXT_A
#define RV32_FEATURE_EXT_A 1
#endif

/* Standard Extension for Compressed Instructions */
#ifndef RV32_FEATURE_EXT_C
#define RV32_FEATURE_EXT_C 1
#endif

/* Standard Extension for Single-Precision Floating Point Instructions */
#ifndef RV32_FEATURE_EXT_F
#define RV32_FEATURE_EXT_F 1
#endif

/* Control and Status Register (CSR) */
#ifndef RV32_FEATURE_Zicsr
#define RV32_FEATURE_Zicsr 1
#endif

/* Instruction-Fetch Fence */
#ifndef RV32_FEATURE_Zifencei
#define RV32_FEATURE_Zifencei 1
#endif

/* Experimental SDL oriented system calls */
#ifndef RV32_FEATURE_SDL
#define RV32_FEATURE_SDL 1
#endif

/* GDB remote debugging */
#ifndef RV32_FEATURE_GDBSTUB
#define RV32_FEATURE_GDBSTUB 1
#endif

/* Import adaptive replacement cache to manage block */
#ifndef RV32_FEATURE_ARC
#define RV32_FEATURE_ARC 0
#endif

/* Cache */
#ifndef RV32_FEATURE_JIT
#define RV32_FEATURE_JIT 0
#endif

/* Feature test macro */
#define RV32_HAS(x) RV32_FEATURE_##x

#endif /* RV32_FEATURES_H */
