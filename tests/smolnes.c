/* smolnes - A NES emulator in ~5000 significant bytes of C.
 *
 * Copyright (c) 2022 Ben Smith
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE
 *
 * Source: https://github.com/binji/smolnes
 *
 * Modified by Alan Jian <alanjian85@outlook.com>
 *  - Replaced SDL functions with rv32emu-specific, SDL-oriented system calls.
 *  - The emulator, by default, attempts to load the file specified in argv[1].
 *    If not found, it falls back to using 'falling.nes'.
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INDEX_RIGHT 0
#define INDEX_LEFT 1
#define INDEX_DOWN 2
#define INDEX_UP 3
#define INDEX_RETURN 4
#define INDEX_TAB 5
#define INDEX_Z 6
#define INDEX_X 7

#define SCREEN_WIDTH 256
#define SCREEN_HEIGHT 240

#define PULL mem(++S, 1, 0, false)
#define PUSH(x) mem(S--, 1, x, true);

#define OP16(x) \
    break;      \
    case x:     \
    case x + 16:

enum {
    KEY_EVENT = 0,
    MOUSE_MOTION_EVENT = 1,
    MOUSE_BUTTON_EVENT = 2,
    QUIT_EVENT = 3
};

typedef struct {
    uint32_t keycode;
    uint8_t state;
} key_event_t;

typedef struct {
    int32_t x, y, xrel, yrel;
} mouse_motion_t;

typedef struct {
    uint8_t button;
    uint8_t state;
} mouse_button_t;

typedef struct {
    uint32_t type;
    union {
        key_event_t key_event;
        union {
            mouse_motion_t motion;
            mouse_button_t button;
        } mouse;
    };
} event_t;

typedef struct {
    event_t *base;
    size_t start;
    size_t capacity;
} event_queue_t;

event_queue_t event_queue = {.base = NULL, .start = 0, .capacity = 0};

enum { RELATIVE_MODE_SUBMISSION = 0, WINDOW_TITLE_SUBMISSION = 1 };

typedef struct {
    uint8_t enabled;
} mouse_submission_t;

typedef struct {
    uint32_t title;
    uint32_t size;
} title_submission_t;

typedef struct {
    uint32_t type;
    union {
        mouse_submission_t mouse;
        title_submission_t title;
    };
} submission_t;

size_t event_count;

/* BGR565 palette. Used instead of RGBA32 to reduce source code size. */
uint32_t rgba[64] = {
    0xFF5C5C5C, 0xFF002267, 0xFF131280, 0xFF2E067E, 0xFF460060, 0xFF530231,
    0xFF510A02, 0xFF411900, 0xFF282900, 0xFF0D3700, 0xFF003E00, 0xFF003C0A,
    0xFF00313B, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFA7A7A7, 0xFF1E55b7,
    0xFF3F3DDA, 0xFF662BD6, 0xFF8822AC, 0xFF9A246B, 0xFF983225, 0xFF814700,
    0xFF5D5F00, 0xFF367300, 0xFF187D00, 0xFF097A32, 0xFF0B6B79, 0xFF000000,
    0xFF000000, 0xFF000000, 0xFFFEFFFF, 0xFF6AA7FF, 0xFF8F8DFF, 0xFFB979FF,
    0xFFDD6FFF, 0xFFF172BE, 0xFFEE8173, 0xFFD69837, 0xFFB0B218, 0xFF86C71C,
    0xFF64D141, 0xFF52CE81, 0xFF54BECD, 0xFF454545, 0xFF000000, 0xFF000000,
    0xFFFEFFFF, 0xFFC0DAFF, 0xFFD0CFFF, 0xFFE2C6FF, 0xFFF1C2FF, 0xFFF9C3E4,
    0xFFF8CAC4, 0xFFEED4A9, 0xFFDEDF9B, 0xFFCCE79D, 0xFFBDECAE, 0xFFB5EACA,
    0xFFB6E4EA, 0xFFB0B0B0, 0xFF000000, 0xFF000000};
int scany,    /* Scanline Y */
    shift_at; /* Attribute shift register */

uint8_t *rom, *chrrom,                /* Points to the start of PRG/CHR ROM */
    prg[2], chr[2],                   /* Current PRG/CHR banks */
    A, X, Y, P = 4, S = ~2, PCH, PCL, /* CPU Registers */
    addr_lo, addr_hi,                 /* Current instruction address */
    nomem,     /* 1 => current instruction doesn't write to memory */
    result,    /* Temp variable */
    val,       /* Current instruction value */
    cross,     /* 1 => page crossing occurred */
    tmp, tmp2, /* Temp variables */
    ppumask, ppuctrl, ppustatus, /* PPU registers */
    ppubuf,                      /* PPU buffered reads */
    W,                           /* Write toggle PPU register */
    fine_x,                      /* X fine scroll offset, 0..7 */
    opcode,                      /* Current instruction opcode */
    nmi,                         /* 1 => NMI occurred */
    ntb,                         /* Nametable byte */
    ptb_lo, ptb_hi,              /* Pattern table low/high byte */
    vram[2048],                  /* Nametable RAM */
    palette_ram[64],             /* Palette RAM */
    ram[8192],                   /* CPU RAM */
    chrram[8192],                /* CHR RAM (only used for some games) */
    prgram[8192],                /* PRG RAM (only used for some games) */
    oam[256],                    /* Object Attribute Memory (sprite RAM) */
    mask[] =
        {128, 64, 1,  2, /* Masks used in branch instructions */
         1,   0,  0,  1, 4, 0, 0, 4,
         0,   0,  64, 0, 8, 0, 0, 8}, /* Masks used in SE* /CL* instructions. */
    keys,                             /* Joypad shift register */
    mirror,                           /* Current mirroring mode */
    mmc1_bits, mmc1_data, mmc1_ctrl,  /* Mapper 1 (MMC1) registers */
    chrbank0, chrbank1, prgbank,      /* Current PRG/CHR bank */
    rombuf[1024 * 1024],              /* Buffer to read ROM file into */
    key_state[8];

uint16_t T, V,          /* "Loopy" PPU registers */
    sum,                /* Sum used for ADC/SBC */
    dot,                /* Horizontal position of PPU, from 0..340 */
    atb,                /* Attribute byte */
    shift_hi, shift_lo, /* Pattern table shift registers */
    cycles;             /* Cycle count for current instruction */

uint32_t frame_buffer[61440]; /* 256x240 pixel frame buffer. Top and bottom 8
                                 rows are not drawn. */

void draw_frame(void *base, int width, int height)
{
    register void *a0 asm("a0") = base;
    register int a1 asm("a1") = width;
    register int a2 asm("a2") = height;
    register size_t a7 asm("a7") = 0xbeef;
    asm volatile("scall" : : "r"(a0), "r"(a1), "r"(a2), "r"(a7));
}

void setup_queue(void *base, size_t capacity)
{
    register void *a0 asm("a0") = base;
    register size_t a1 asm("a1") = capacity;
    register size_t *a2 asm("a2") = &event_count;
    register size_t a7 asm("a7") = 0xc0de;
    asm volatile("scall" : : "r"(a0), "r"(a1), "r"(a2), "r"(a7));

    event_queue.base = base;
    event_queue.capacity = capacity;
    /* Ignore the subsequent submission queue */
}

int poll_event(event_t *event)
{
    if (event_count == 0)
        return 0;

    *event = event_queue.base[event_queue.start];
    ++event_queue.start;
    event_queue.start &= event_queue.capacity - 1;
    --event_count;

    return 1;
}

/* Read a byte from CHR ROM or CHR RAM. */
static inline uint8_t *get_chr_byte(uint16_t a)
{
    return &chrrom[chr[a >> 12] << 12 | a & 4095];
}

/* Read a byte from nametable RAM. */
static inline uint8_t *get_nametable_byte(uint16_t a)
{
    return &vram[!mirror       ? a % 1024        /* single bank 0 */
                 : mirror == 1 ? a % 1024 + 1024 /* single bank 1 */
                 : mirror == 2
                     ? a & 2047                  /* vertical mirroring */
                     : a / 2 & 1024 | a % 1024]; /* horizontal mirroring */
}

/* If `write` is non-zero, writes `val` to the address `hi:lo`, otherwise reads
 * a value from the address `hi:lo`.
 */
static inline uint8_t mem(uint8_t lo, uint8_t hi, uint8_t val, bool is_write)
{
    uint16_t a = hi << 8 | lo;

    switch (hi >> 4) {
    case 0 ... 1: /* $0000...$1fff RAM */
        return is_write ? ram[a] = val : ram[a];

    case 2 ... 3: /* $2000..$2007 PPU (mirrored) */
        lo &= 7;

        /* read/write $2007 */
        if (lo == 7) {
            tmp = ppubuf;
            uint8_t *rom =
                /* Access CHR ROM or CHR RAM */
                V < 8192
                    ? !is_write || chrrom == chrram ? get_chr_byte(V) : &tmp2
                /* Access nametable RAM */
                : V < 16128
                    ? get_nametable_byte(V)
                    /* Access palette RAM */
                    : palette_ram + (uint8_t) ((V & 19) == 16 ? V ^ 16 : V);
            is_write ? *rom = val : (ppubuf = *rom);
            V += ppuctrl & 4 ? 32 : 1;
            V %= 16384;
            return tmp;
        }

        if (is_write)
            switch (lo) {
            case 0: /* $2000 ppuctrl */
                ppuctrl = val;
                T = T & 62463 | val % 4 << 10;
                break;

            case 1: /* $2001 ppumask */
                ppumask = val;
                break;

            case 5: /* $2005 ppuscroll */
                T = (W ^= 1)      ? fine_x = val & 7,
                T & ~31 | val / 8 : T & 35871 | val % 8 << 12 | (val & 248) * 4;
                break;

            case 6: /* $2006 ppuaddr */
                T = (W ^= 1) ? T & 255 | val % 64 << 8 : (V = T & ~255 | val);
            }

        if (lo == 2) /* $2002 ppustatus */
            return tmp = ppustatus & 224, ppustatus &= 127, W = 0, tmp;

        break;

    case 4:
        if (is_write && lo == 20) /* $4014 OAM DMA */
            for (sum = 256; sum--;)
                oam[sum] = mem(sum, val, 0, false);
        /* $4016 Joypad 1 */
        return (lo == 22) ? is_write ? keys = (key_state[INDEX_RIGHT] * 8 +
                                               key_state[INDEX_LEFT] * 4 +
                                               key_state[INDEX_DOWN] * 2 +
                                               key_state[INDEX_UP]) *
                                                  16 +
                                              key_state[INDEX_RETURN] * 8 +
                                              key_state[INDEX_TAB] * 4 +
                                              key_state[INDEX_Z] * 2 +
                                              key_state[INDEX_X]
                                     : (tmp = keys & 1, keys /= 2, tmp)
                          : 0;

    case 6 ... 7: /* $6000...$7fff PRG RAM */
        return is_write ? prgram[a & 8191] = val : prgram[a & 8191];

    case 8 ... 15: /* $8000...$ffff ROM */
        /* handle mmc1 writes */
        if (is_write)
            switch (rombuf[6] >> 4) {
            case 7: /* mapper 7 */
                mirror = !(val / 16);
                *prg = val = val % 8 * 2;
                prg[1] = val + 1;
                break;

            case 3: /* mapper 3 */
                *chr = val = val % 4 * 2;
                chr[1] = val + 1;
                break;

            case 2: /* mapper 2 */
                *prg = val & 31;
                break;

            case 1: /* mapper 1 */
                if (val & 128) {
                    mmc1_bits = 5, mmc1_data = 0, mmc1_ctrl |= 12;
                } else if (mmc1_data = mmc1_data / 2 | val << 4 & 16,
                           !--mmc1_bits) {
                    mmc1_bits = 5, tmp = a >> 13;
                    *(tmp == 4 ? mirror = mmc1_data & 3, &mmc1_ctrl
                  : tmp == 5   ? &chrbank0
                  : tmp == 6   ? &chrbank1
                               : &prgbank) = mmc1_data;

                    /* Update CHR banks. */
                    *chr = chrbank0 & ~!(mmc1_ctrl & 16);
                    chr[1] = mmc1_ctrl & 16 ? chrbank1 : chrbank0 | 1;

                    /* Update PRG banks. */
                    tmp = mmc1_ctrl / 4 & 3;
                    *prg = tmp == 2 ? 0 : tmp == 3 ? prgbank : prgbank & ~1;
                    prg[1] = tmp == 2   ? prgbank
                             : tmp == 3 ? rombuf[4] - 1
                                        : prgbank | 1;
                }
            }
        return rom[prg[(a >> 14) - 2] << 14 | a & 16383];
    }

    return ~0;
}

/* Read a byte at address `PCH:PCL` and increment PC. */
static inline uint8_t read_pc()
{
    val = mem(PCL, PCH, 0, false);
    !++PCL ? ++PCH : 0;
    return val;
}

/* Set N (negative) and Z (zero) flags of `P` register, based on `val`. */
static inline uint8_t set_nz(uint8_t val)
{
    return P = P & ~130 | val & 128 | !val * 2;
}

#include "falling-nes.h"

int main(int argc, char **argv)
{
    FILE *file = NULL;
    if (argc == 2) {
        file = fopen(argv[1], "rb");
        fread(rombuf, 1024 * 1024, 1, file);
        fclose(file);
    } else {
        /* fallback */
        memcpy(rombuf, falling_nes, sizeof(falling_nes));
    }

    void *base = malloc(sizeof(event_t) * 128 + sizeof(submission_t) * 128);
    setup_queue(base, 128);

    /* Start PRG0 after 16-byte header. */
    rom = rombuf + 16;
    /* PRG1 is the last bank. `rombuf[4]` is the number of 16k PRG banks. */
    prg[1] = rombuf[4] - 1;
    /* CHR0 ROM is after all PRG data in the file. `rombuf[5]` is the number of
     * 8k CHR banks. If it is zero, assume the game uses CHR RAM.
     */
    chrrom = rombuf[5] ? rom + ((prg[1] + 1) << 14) : chrram;
    /* CHR1 is the last 4k bank. */
    chr[1] = (rombuf[5] ? rombuf[5] : 1) * 2 - 1;
    /* Bit 0 of rombuf[6] is 0: horizontal mirroring, 1: vertical mirroring. */
    mirror = !(rombuf[6] & 1) + 2;

    /* Start at address in reset vector, at $FFFC. */
    PCL = mem(~3, ~0, 0, false);
    PCH = mem(~2, ~0, 0, false);

    for (;;) {
        cycles = nomem = 0;
        if (nmi)
            goto nmi;

        switch ((opcode = read_pc()) & 31) {
        case 0:
            if (opcode & 128) { /* LDY/CPY/CPX imm */
                read_pc();
                nomem = 1;
                goto nomemop;
            }

            switch (opcode >> 5) {
            case 0: /* BRK or NMI */
                !++PCL ? ++PCH : 0;
            nmi:
                PUSH(PCH)
                PUSH(PCL)
                PUSH(P | 32)
                /* BRK vector is $ffff, NMI vector is $fffa */
                PCL = mem(~1 - nmi * 4, ~0, 0, false);
                PCH = mem(~0 - nmi * 4, ~0, 0, false);
                cycles++;
                nmi = 0;
                break;

            case 1: /* JSR */
                result = read_pc();
                PUSH(PCH)
                PUSH(PCL)
                PCH = read_pc();
                PCL = result;
                break;

            case 2: /* RTI */
                P = PULL & ~32;
                PCL = PULL;
                PCH = PULL;
                break;

            case 3: /* RTS */
                PCL = PULL;
                PCH = PULL;
                !++PCL ? ++PCH : 0;
                break;
            }

            cycles += 4;
            break;

        case 16: /* BPL, BMI, BVC, BVS, BCC, BCS, BNE, BEQ */
            read_pc();
            if (!(P & mask[opcode >> 6 & 3]) ^ opcode / 32 & 1) {
                if (cross = PCL + (int8_t) val >> 8)
                    PCH += cross, cycles++;
                cycles++, PCL += (int) val;
            }

            OP16(8)
            switch (opcode >>= 4) {
            case 0: /* PHP */
                PUSH(P | 48)
                cycles++;
                break;

            case 2: /* PLP */
                P = PULL & ~16;
                cycles += 2;
                break;

            case 4: /* PHA */
                PUSH(A)
                cycles++;
                break;

            case 6: /* PLA */
                set_nz(A = PULL);
                cycles += 2;
                break;

            case 8: /* DEY */
                set_nz(--Y);
                break;

            case 9: /* TYA */
                set_nz(A = Y);
                break;

            case 10: /* TAY */
                set_nz(Y = A);
                break;

            case 12: /* INY */
                set_nz(++Y);
                break;

            case 14: /* INX */
                set_nz(++X);
                break;

            default: /* CLC, SEC, CLI, SEI, CLV, CLD, SED */
                P = P & ~mask[opcode + 3] | mask[opcode + 4];
                break;
            }

            OP16(10)
            switch (opcode >> 4) {
            case 8: /* TXA */
                set_nz(A = X);
                break;

            case 9: /* TXS */
                S = X;
                break;

            case 10: /* TAX */
                set_nz(X = A);
                break;

            case 11: /* TSX */
                set_nz(X = S);
                break;

            case 12: /* DEX */
                set_nz(--X);
                break;

            case 14: /* NOP */
                break;

            default: /* ASL/ROL/LSR/ROR A */
                nomem = 1;
                val = A;
                goto nomemop;
            }
            break;

        case 1: /* X-indexed, indirect */
            read_pc();
            val += X;
            addr_lo = mem(val, 0, 0, false);
            addr_hi = mem(val + 1, 0, 0, false);
            cycles += 4;
            goto opcode;

        case 4 ... 6: /* Zeropage */
            addr_lo = read_pc();
            addr_hi = 0;
            cycles++;
            goto opcode;

        case 2:
        case 9: /* Immediate */
            read_pc();
            nomem = 1;
            goto nomemop;

        case 12 ... 14: /* Absolute */
            addr_lo = read_pc();
            addr_hi = read_pc();
            cycles += 2;
            goto opcode;

        case 17: /* Zeropage, Y-indexed */
            addr_lo = mem(read_pc(), 0, 0, false);
            addr_hi = mem(val + 1, 0, 0, false);
            val = Y;
            tmp = opcode == 145; /* STA always uses extra cycle. */
            cycles++;
            goto cross;

        case 20 ... 22: /* Zeropage, X-indexed */
            addr_lo =
                read_pc() + ((opcode & 214) == 150 ? Y : X); /* LDX/STX use Y */
            addr_hi = 0;
            cycles += 2;
            goto opcode;

        case 25: /* Absolute, Y-indexed. */
            addr_lo = read_pc();
            addr_hi = read_pc();
            val = Y;
            tmp = opcode == 153; /* STA always uses extra cycle. */
            goto cross;

        case 28 ... 30: /* Absolute, X-indexed. */
            addr_lo = read_pc();
            addr_hi = read_pc();
            val = opcode == 190 ? Y : X; /* LDX uses Y */
            tmp = opcode ==
                      157 || /* STA always uses extra cycle.
                                ASL/ROL/LSR/ROR/INC/DEC all uses extra cycle. */
                  opcode % 16 == 14 && opcode != 190;
            /* fallthrough */
        cross:
            addr_hi += cross = addr_lo + val > 255;
            addr_lo += val;
            cycles += 2 + tmp | cross;
            /* fallthrough */

        opcode:
            /* Read from the given address into `val` for convenience below,
             * except for the STA/STX/STY instructions, and JMP.
             */
            if ((opcode & 224) != 128 && opcode != 76)
                val = mem(addr_lo, addr_hi, 0, false);

        nomemop:
            switch (opcode & 243) {
                OP16(1) set_nz(A |= val);  /* ORA */
                OP16(33) set_nz(A &= val); /* AND */
                OP16(65) set_nz(A ^= val); /* EOR */

                OP16(225) /* SBC */
                val = ~val;
                goto add;

                OP16(97) /* ADC */
            add:
                sum = A + val + (P & 1);
                P = P & ~65 | sum > 255 | (~(A ^ val) & (val ^ sum) & 128) / 2;
                set_nz(A = sum);

                OP16(2) /* ASL */
                result = val * 2;
                P = P & ~1 | val / 128;
                goto memop;

                OP16(34) /* ROL */
                result = val * 2 | P & 1;
                P = P & ~1 | val / 128;
                goto memop;

                OP16(66) /* LSR */
                result = val / 2;
                P = P & ~1 | val & 1;
                goto memop;

                OP16(98) /* ROR */
                result = val / 2 | P << 7;
                P = P & ~1 | val & 1;
                goto memop;

                OP16(194) /* DEC */
                result = val - 1;
                goto memop;

                OP16(226) /* INC */
                result = val + 1;
                /* fallthrough */

            memop:
                set_nz(result);
                /* Write result to A or back to memory. */
                nomem ? A = result
                      : (cycles += 2, mem(addr_lo, addr_hi, result, true));
                break;

            case 32: /* BIT */
                P = P & 61 | val & 192 | !(A & val) * 2;
                break;

            case 64: /* JMP */
                PCL = addr_lo;
                PCH = addr_hi;
                cycles--;
                break;

            case 96: /* JMP indirect */
                PCL = val;
                PCH = mem(addr_lo + 1, addr_hi, 0, false);
                cycles++;

                OP16(160) set_nz(Y = val); /* LDY */
                OP16(161) set_nz(A = val); /* LDA */
                OP16(162) set_nz(X = val); /* LDX */

                OP16(128) result = Y;
                goto store; /* STY */
                OP16(129) result = A;
                goto store;           /* STA */
                OP16(130) result = X; /* STX */

            store:
                mem(addr_lo, addr_hi, result, true);

                OP16(192) result = Y;
                goto cmp; /* CPY */
                OP16(193) result = A;
                goto cmp;             /* CMP */
                OP16(224) result = X; /* CPX */
            cmp:
                P = P & ~1 | result >= val;
                set_nz(result - val);
                break;
            }
        }

        /* Update PPU, which runs 3 times faster than CPU. Each CPU instruction
         * takes at least 2 cycles.
         */
        for (tmp = cycles * 3 + 6; tmp--;) {
            /* If background or sprites are not enabled. */
            if (!(ppumask & 24))
                goto handle_events;

            if (scany < 240) {
                if (dot < 256 || dot > 319) {
                    switch (dot & 7) {
                    case 1: /* Read nametable byte. */
                        ntb = *get_nametable_byte(V);
                        break;
                    case 3: /* Read attribute byte. */
                        atb = (*get_nametable_byte(960 | V & 3072 |
                                                   V >> 4 & 56 | V / 4 & 7) >>
                               (V >> 5 & 2 | V / 2 & 1) * 2) &
                              3;
                        {
                            static const uint16_t lut[] = {0x0, 0x5555, 0xaaaa,
                                                           0xffff};
                            atb = lut[atb];
                        }
                        break;
                    case 5: /* Read pattern table low byte. */
                        ptb_lo = *get_chr_byte(ppuctrl << 8 & 4096 | ntb << 4 |
                                               V >> 12);
                        break;
                    case 7: /* Read pattern table high byte. */
                        ptb_hi = *get_chr_byte(ppuctrl << 8 & 4096 | ntb << 4 |
                                               V >> 12 | 8);
                        /* Increment horizontal VRAM read address. */
                        V = (V & 31) == 31 ? V & ~31 ^ 1024 : V + 1;
                        break;
                    }

                    /* Draw a pixel to the framebuffer. */
                    if ((uint16_t) scany < 240 && dot < 256) {
                        /* Read color and palette from shift registers. */
                        uint8_t color = shift_hi >> 14 - fine_x & 2 |
                                        shift_lo >> 15 - fine_x & 1,
                                palette = shift_at >> 28 - fine_x * 2 & 12;

                        /* If sprites are not enabled */
                        if (!(ppumask & 16))
                            continue;

                        /* Loop through all sprites. */
                        for (uint8_t *sprite = oam; sprite < oam + 256;
                             sprite += 4) {
                            uint16_t sprite_h = ppuctrl & 32 ? 16 : 8,
                                     sprite_x = dot - sprite[3],
                                     sprite_y = scany - *sprite - 1,
                                     sx = sprite_x ^ (sprite[2] & 64 ? 0 : 7),
                                     sy = sprite_y ^
                                          (sprite[2] & 128 ? sprite_h - 1 : 0);
                            if (sprite_x >= 8 || sprite_y >= sprite_h)
                                continue;

                            uint16_t sprite_tile = sprite[1],
                                     sprite_addr =
                                         ppuctrl & 32
                                             /* 8x16 sprites */
                                             ? sprite_tile % 2 << 12 |
                                                   (sprite_tile & ~1) << 4 |
                                                   (sy & 8) * 2 | sy & 7
                                             /* 8x8 sprites */
                                             : (ppuctrl & 8) << 9 |
                                                   sprite_tile << 4 | sy & 7,
                                     sprite_color =
                                         *get_chr_byte(sprite_addr + 8) >>
                                                 sx << 1 &
                                             2 |
                                         *get_chr_byte(sprite_addr) >> sx & 1;
                            /* Only draw sprite if color is not 0
                               (transparent) */
                            if (sprite_color) {
                                /* Don't draw sprite if BG has
                                   priority. */
                                !(sprite[2] & 32 && color)
                                    ? color = sprite_color,
                                      palette = 16 | sprite[2] * 4 & 12 : 0;
                                /* Maybe set sprite0 hit flag. */
                                sprite == oam &&color ? ppustatus |= 64 : 0;
                                break;
                            }
                        }

                        /* Write pixel to framebuffer. Always use palette 0
                           for color 0. */
                        frame_buffer[scany * 256 + dot] =
                            rgba[palette_ram[color ? palette | color : 0]];
                    }

                    /* Update shift registers every cycle. */
                    if (dot < 336) {
                        shift_hi *= 2, shift_lo *= 2, shift_at *= 4;
                    }

                    /* Reload shift registers every 8 cycles. */
                    if (dot % 8 == 7) {
                        shift_hi |= ptb_hi, shift_lo |= ptb_lo, shift_at |= atb;
                    }
                }

                /* Increment vertical VRAM address. */
                if (dot == 256) {
                    V = ((V & 7 << 12) != 7 << 12 ? V + 4096
                         : (V & 992) == 928       ? V & 35871 ^ 2048
                         : (V & 992) == 992       ? V & 35871
                                                  : V & 35871 | V + 32 & 992) &
                            /* Reset horizontal VRAM address to T value. */
                            ~1055 |
                        T & 1055;
                }
            }

            /* Reset vertical VRAM address to T value. */
            if (scany == -1 && dot > 279 && dot < 305)
                V = V & 33823 | T & 31712;

        handle_events:
            if (scany == 241 && dot == 1) {
                /* If NMI is enabled, trigger NMI. */
                ppuctrl & 128 ? nmi = 1 : 0;
                ppustatus |= 128;
                /* Render frame, skipping the top and bottom 8 pixels (they're
                   often garbage). */
                draw_frame(frame_buffer + 4096, 256, 224);
                /* Handle events. */
                for (event_t event; poll_event(&event);)
                    switch (event.type) {
                    case KEY_EVENT: {
                        uint8_t state = event.key_event.state;
                        switch (event.key_event.keycode) {
                        case 0x4000004F:
                            key_state[INDEX_RIGHT] = state;
                            break;
                        case 0x40000050:
                            key_state[INDEX_LEFT] = state;
                            break;
                        case 0x40000051:
                            key_state[INDEX_DOWN] = state;
                            break;
                        case 0x40000052:
                            key_state[INDEX_UP] = state;
                            break;
                        case '\xd':
                            key_state[INDEX_RETURN] = state;
                            break;
                        case '\t':
                            key_state[INDEX_TAB] = state;
                            break;
                        case 'z':
                            key_state[INDEX_Z] = state;
                            break;
                        case 'x':
                            key_state[INDEX_X] = state;
                            break;
                        }
                        break;
                    }
                    case QUIT_EVENT:
                        return 0;
                    }
            }

            /* Clear ppustatus. */
            scany == -1 &&dot == 1 ? ppustatus = 0 : 0;

            /* Increment to next dot/scany. 341 dots per scanline, 262 scanlines
             * per frame. Scanline 261 is represented as -1.
             */
            ++dot == 341 ? dot = 0, scany = scany == 260 ? -1 : scany + 1 : 0;
        }
    }
}
