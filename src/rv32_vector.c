/* RV32 RVV Instruction Set */


RVOP(vsetvli, { 
    // | `vlmul[2:0]` | LMUL   | #groups | VLMAX           | Registers grouped with register `n`              |
    // |--------------|--------|---------|-----------------|--------------------------------------------------|
    // | `1 0 0`      | -      | -       | -               | Reserved                                         |
    // | `1 0 1`      | `1/8`  | 32      | `VLEN/SEW/8`    | `v_n` (single register in group)                |
    // | `1 1 0`      | `1/4`  | 32      | `VLEN/SEW/4`    | `v_n` (single register in group)                |
    // | `1 1 1`      | `1/2`  | 32      | `VLEN/SEW/2`    | `v_n` (single register in group)                |
    // | `0 0 0`      | `1`    | 32      | `VLEN/SEW`      | `v_n` (single register in group)                |
    // | `0 0 1`      | `2`    | 16      | `2*VLEN/SEW`    | `v_n`, `v_n+1`                                  |
    // | `0 1 0`      | `4`    | 8       | `4*VLEN/SEW`    | `v_n`, ..., `v_n+3`                             |
    // | `0 1 1`      | `8`    | 4       | `8*VLEN/SEW`    | `v_n`, ..., `v_n+7`                             |
    rv->lmul = 1<<(ir->zimm & 0x3);
    if (ir->zimm & 0x4) {
        rv->lmul = 1;
    }

    // | `vsew[2:0]` | SEW      |
    // |-------------|----------|
    // | 0 0 0       | 8        |
    // | 0 0 1       | 16       |
    // | 0 1 0       | 32       |
    // | 0 1 1       | 64       |
    // | 1 X X       | Reserved |
    rv->sew = 8 << (ir->zimm & 0xf); 
    }, GEN({/* no operation */}))

#define ADD_VV(BIT)                                        \
  static inline *int##BIT##_t add_vv_i##BIT##(int##BIT##_t *a, int##BIT##_t *b, size_t size) {    \
    int##BIT##_t c[size];\
    for (int i = 0; i < size; i++) {\
        c[i]=a[i]+b[i];\
    }\
    return c;\
  }                                                                       
ADD_VV(8)
ADD_VV(16)
ADD_VV(32)
ADD_VV(64)


RVOP(vadd_vi, { rv->V1[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vadd_vv, { 
    switch (rv->sew) {
    case 8:        
        rv->Vd = (* int8_t)add_vv_i8((*int8_t)rv->V1, (*int8_t)rv->V2, rv-vl/8);
        break;
    case 16:        
    rv->Vd = (* int8_t)add_vv_i16((*int16_t)rv->V1, (*int16_t)rv->V2, rv-vl/16);
        break;
    case 32:        
    rv->Vd = (* int8_t)add_vv_i32((*int32_t)rv->V1, (*int32_t)rv->V2, rv-vl/32);
        break;
    case 64:        
    rv->Vd = (* int8_t)add_vv_i64((*int64_t)rv->V1, (*int64_t)rv->V2, rv-vl/64);
        break;
    
    default:
        break;
    }

}, GEN({/* no operation */}))
RVOP(vand_vi, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vand_vv, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vmadc_vi, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vmadc_vv, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vmseq_vi, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vmseq_vv, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vmsgt_vi, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vmsgt_vv, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vmsgtu_vi, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vmsgtu_vv, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vmsle_vi, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vmsle_vv, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vmsleu_vi, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vmsleu_vv, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vmsne_vi, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vmsne_vv, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vor_vi, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vor_vv, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vrgather_vi, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vrgather_vv, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vrsub_vi, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vrsub_vv, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vsadd_vi, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vsadd_vv, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vsaddu_vi, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vsaddu_vv, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vslidedown_vi, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vslidedown_vv, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vslideup_vi, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vslideup_vv, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vsll_vi, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vsll_vv, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vsra_vi, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vsra_vv, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vsrl_vi, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vsrl_vv, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vssra_vi, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vssra_vv, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vssrl_vi, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vssrl_vv, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vxor_vi, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
RVOP(vxor_vv, { rv->Vd[rv_reg_zero] = 0; }, GEN({/* no operation */}))
