#include <stdint.h>

// opcode = 0
uint32_t caddi4spn_to_addi(uint32_t inst);
uint32_t clw_to_lw(uint32_t inst);
uint32_t csw_to_sw(uint32_t inst);

// opcode = 1
uint32_t cnop_to_addi();
uint32_t caddi_to_addi(uint32_t inst);
uint32_t cjal_to_jal(uint32_t inst);
uint32_t cli_to_addi(uint32_t inst);
uint32_t caddi16sp_to_addi(uint32_t inst);
uint32_t clui_to_lui(uint32_t inst);
uint32_t csrli_to_srli(uint32_t inst);
uint32_t csrai_to_srai(uint32_t inst);
uint32_t candi_to_andi(uint32_t inst);
uint32_t csub_to_sub(uint32_t inst);
uint32_t cxor_to_xor(uint32_t inst);
uint32_t cor_to_or(uint32_t inst);
uint32_t cand_to_and(uint32_t inst);
uint32_t cj_to_jal(uint32_t inst);
uint32_t cbeqz_to_beq(uint32_t inst);
uint32_t cbenz_to_bne(uint32_t inst);

// opcode = 2
uint32_t cslli_to_slli(uint32_t inst);
uint32_t clwsp_to_lw(uint32_t inst);
uint32_t cjr_to_jalr(uint32_t inst);
uint32_t cmv_to_add(uint32_t inst);
uint32_t cjalr_to_jalr(uint32_t inst);
uint32_t cebreak_to_ebreak();
uint32_t cadd_to_add(uint32_t inst);
uint32_t cswsp_to_sw(uint32_t inst);

uint32_t parse_011_01(uint32_t inst);
uint32_t parse_100_01(uint32_t inst);
uint32_t parse_100_10(uint32_t inst);