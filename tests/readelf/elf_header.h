#include "elf.h"
#include "elf_ident.h"

#ifndef ELF_HEADER_H
#define ELF_HEADER_H

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    Elf32_Half e_type;
    Elf32_Half e_machine;
    Elf32_Word e_version;
    Elf32_Addr e_entry;
    Elf32_Off e_phoff;
    Elf32_Off e_shoff;
    Elf32_Word e_flags;
    Elf32_Half e_ehsize;
    Elf32_Half e_phentsize;
    Elf32_Half e_phnum;
    Elf32_Half e_shentsize;
    Elf32_Half e_shnum;
    Elf32_Half e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    Elf64_Half e_type;
    Elf64_Half e_machine;
    Elf64_Word e_version;
    Elf64_Addr e_entry;
    Elf64_Off e_phoff;
    Elf64_Off e_shoff;
    Elf64_Word e_flags;
    Elf64_Half e_ehsize;
    Elf64_Half e_phentsize;
    Elf64_Half e_phnum;
    Elf64_Half e_shentsize;
    Elf64_Half e_shnum;
    Elf64_Half e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    // ELF identification
    unsigned char e_ident[EI_NIDENT];
    // object file type
    uint16_t e_type;
    // architecture
    uint16_t e_machine;
    // object file version
    uint32_t e_version;
    // virtual address to starting the process
    uint64_t e_entry;
    // program header table's offset in bytes
    uint64_t e_phoff;
    // section header table's offset in bytes
    uint64_t e_shoff;
    // processor-specific flags
    uint32_t e_flags;
    // ELF header's size in bytes
    uint16_t e_ehsize;
    // size in bytes of one entry in the program header table
    uint16_t e_phentsize;
    // number of entries in the program header table
    uint16_t e_phnum;
    // size in bytes of one entry in the section header table
    uint16_t e_shentsize;
    // number of entries in the section header table
    uint16_t e_shnum;
    // section nam string table's index in the section header table
    uint16_t e_shstrndx;
} Elf_Internal_Ehdr;

/**
 * Object File Type
 */
#define ET_NONE 0
#define ET_REL 1
#define ET_EXEC 2
#define ET_DYN 3
#define ET_CORE 4
#define ET_LOOS 0xfe00
#define ET_HIOS 0xfeff
#define ET_LOPROC 0xff00
#define ET_HIPROC 0xffff

/**
 * Machine Architecture
 */
#define EM_NONE 0
#define EM_M32 1
#define EM_SPARC 2
#define EM_386 3
#define EM_68K 4
#define EM_88K 5
#define EM_IAMCU 6
#define EM_860 7
#define EM_MIPS 8
#define EM_S370 9
#define EM_MIPS_RS3_LE 10
#define EM_PARISC 15
#define EM_VPP500 17
#define EM_SPARC32PLUS 18
#define EM_960 19
#define EM_PPC 20
#define EM_PPC64 21
#define EM_S390 22
#define EM_SPU 23
#define EM_V800 36
#define EM_FR20 37
#define EM_RH32 38
#define EM_RCE 39
#define EM_ARM 40
#define EM_ALPHA 41
#define EM_SH 42
#define EM_SPARCV9 43
#define EM_TRICORE 44
#define EM_ARC 45
#define EM_H8_300 46
#define EM_H8_300H 47
#define EM_H8S 48
#define EM_H8_500 49
#define EM_IA_64 50
#define EM_MIPS_X 51
#define EM_COLDFIRE 52
#define EM_68HC12 53
#define EM_MMA 54
#define EM_PCP 55
#define EM_NCPU 56
#define EM_NDR1 57
#define EM_STARCORE 58
#define EM_ME16 59
#define EM_ST100 60
#define EM_TINYJ 61
#define EM_X86_64 62
#define EM_PDSP 63
#define EM_PDP10 64
#define EM_PDP11 65
#define EM_FX66 66
#define EM_ST9PLUS 67
#define EM_ST7 68
#define EM_68HC16 69
#define EM_68HC11 70
#define EM_68HC08 71
#define EM_68HC05 72
#define EM_SVX 73
#define EM_ST19 74
#define EM_VAX 75
#define EM_CRIS 76
#define EM_JAVELIN 77
#define EM_FIREPATH 78
#define EM_ZSP 79
#define EM_MMIX 80
#define EM_HUANY 81
#define EM_PRISM 82
#define EM_AVR 83
#define EM_FR30 84
#define EM_D10V 85
#define EM_D30V 86
#define EM_V850 87
#define EM_M32R 88
#define EM_MN10300 89
#define EM_MN10200 90
#define EM_PJ 91
#define EM_OPENRISC 92
#define EM_ARC_COMPACT 93
#define EM_XTENSA 94
#define EM_VODEPCPRE 95
#define EM_TMM_GPP 96
#define EM_NS32K 97
#define EM_TPC 98
#define EM_SNP1K 99
#define EM_ST200 100
#define EM_IP2K 101
#define EM_MAX 102
#define EM_CR 103
#define EM_F2MC16 104
#define EM_MSP430 105
#define EM_BLACKFIN 106
#define EM_SE_C33 107
#define EM_SEP 108
#define EM_ARCA 109
#define EM_UNICORE 110
#define EM_EXCESS 111
#define EM_DXP 112
#define EM_ALTERA_NIOS2 113
#define EM_CRX 114
#define EM_XGATE 115
#define EM_C116 116
#define EM_M16C 117
#define EM_DSPIC30F 118
#define EM_CE 119
#define EM_M32C 120
#define EM_TSK3000 131
#define EM_RS08 132
#define EM_SHARC 133
#define EM_ECOG2 134
#define EM_SCORE7 135
#define EM_DSP24 136
#define EM_VIDEOCORE3 137
#define EM_LATTINCEMICO32 138
#define EM_SE_C17 139
#define EM_TI_C6000 140
#define EM_TI_C2000 141
#define EM_TI_C5500 142
#define EM_TI_ARP32 143
#define EM_TI_PRU 144
#define EM_MMDSP_PLUS 160
#define EM_CYPRESS_M8C 161
#define EM_R32C 162
#define EM_TRIMEDIA 163
#define EM_QDSP6 164
#define EM_8051 165
#define EM_STXP7X 166
#define EM_NDS32 167
#define EM_ECOG1 168
#define EM_ECOG1X 168
#define EM_MAXQ30 169
#define EM_XIMO16 170
#define EM_MANIK 171
#define EM_CRAYNV2 172
#define EM_RX 173
#define EM_METAG 174
#define EM_MCST_ELBRUS 175
#define EM_ECOG16 176
#define EM_CR16 177
#define EM_ETPU 178
#define EM_SLE9X 179
#define EM_L10M 180
#define EM_K10M 181
#define EM_AARCH64 183
#define EM_AVR32 195
#define EM_STM8 186
#define EM_TILE64 187
#define EM_TILEPRO 188
#define EM_MICROBLAZE 189
#define EM_CUDA 190
#define EM_TILEGX 191
#define EM_CLOUDSHIELD 192
#define EM_COREA_1ST 193
#define EM_COREA_2ND 194
#define EM_ARC_COMPACT2 195
#define EM_OPEN8 196
#define EM_RL78 197
#define EM_VIDEOCORE5 198
#define EM_78KOR 199
#define EM_56800EX 200
#define EM_BA1 201
#define EM_BA2 202
#define EM_XCORE 203
#define EM_MCHP_PIC 204
#define EM_INTEL205 205
#define EM_INTEL206 206
#define EM_INTEL207 207
#define EM_INTEL208 208
#define EM_INTEL209 209
#define EM_KM32 210
#define EM_KMX32 211
#define EM_KMX16 212
#define EM_KMX8 213
#define EM_KVARC 214
#define EM_CDP 215
#define EM_COGE 216
#define EM_COOL 217
#define EM_NORC 218
#define EM_CSR_KALIMBA 219
#define EM_Z80 220
#define EM_VISIUM 221
#define EM_FT32 222
#define EM_MOXIE 223
#define EM_AMDGPU 224
#define EM_RISCV 243

/**
 * Object File Version
 */
#define EV_NONE 0
#define EV_CURRENT 1

Elf_Internal_Ehdr *get_elf_header(FILE *);

void display_file_header(const Elf_Internal_Ehdr *);

#endif  // ELF_HEADER_H
