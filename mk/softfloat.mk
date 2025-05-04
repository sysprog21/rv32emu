SOFTFLOAT_DIR := src/softfloat/source

# FIXME: Suppress compilation warnings in upstream SoftFloat 3
CFLAGS_softfloat := \
    -Wno-unused-parameter \
    -Wno-unused-variable \
    -Wno-sign-compare \
    -Wno-implicit-fallthrough \
    -Wno-uninitialized \
    -I$(SOFTFLOAT_DIR)/RISCV \
    -I$(SOFTFLOAT_DIR)/include

# FIXME: make the flags configurable
CFLAGS_softfloat += \
    -I$(OUT)/softfloat \
    -D LITTLEENDIAN=1 \
    -D INLINE_LEVEL=5 \
    -D SOFTFLOAT_ROUND_ODD \
    -D SOFTFLOAT_FAST_INT64 \
    -D SOFTFLOAT_FAST_DIV64TO32 \
    -D INLINE='static inline'

SOFTFLOAT_OBJS_PRIMITIVES = \
    s_eq128.o \
    s_le128.o \
    s_lt128.o \
    s_shortShiftLeft128.o \
    s_shortShiftRight128.o \
    s_shortShiftRightJam64.o \
    s_shortShiftRightJam64Extra.o \
    s_shortShiftRightJam128.o \
    s_shortShiftRightJam128Extra.o \
    s_shiftRightJam32.o \
    s_shiftRightJam64.o \
    s_shiftRightJam64Extra.o \
    s_shiftRightJam128.o \
    s_shiftRightJam128Extra.o \
    s_shiftRightJam256M.o \
    s_countLeadingZeros8.o \
    s_countLeadingZeros16.o \
    s_countLeadingZeros32.o \
    s_countLeadingZeros64.o \
    s_add128.o \
    s_add256M.o \
    s_sub128.o \
    s_sub256M.o \
    s_mul64ByShifted32To128.o \
    s_mul64To128.o \
    s_mul128By32.o \
    s_mul128To256M.o \
    s_approxRecip_1Ks.o \
    s_approxRecip32_1.o \
    s_approxRecipSqrt_1Ks.o \
    s_approxRecipSqrt32_1.o

SOFTFLOAT_OBJS_SPECIALIZE = \
    RISCV/softfloat_raiseFlags.o \
    RISCV/s_propagateNaNF16UI.o \
    RISCV/s_propagateNaNF32UI.o \
    RISCV/s_propagateNaNF64UI.o \
    RISCV/s_propagateNaNF128UI.o

SOFTFLOAT_OBJS_OTHERS = \
    s_roundToUI32.o \
    s_roundToUI64.o \
    s_roundToI32.o \
    s_roundToI64.o \
    s_normSubnormalF16Sig.o \
    s_roundPackToF16.o \
    s_normRoundPackToF16.o \
    s_addMagsF16.o \
    s_subMagsF16.o \
    s_mulAddF16.o \
    s_normSubnormalF32Sig.o \
    s_roundPackToF32.o \
    s_normRoundPackToF32.o \
    s_addMagsF32.o \
    s_subMagsF32.o \
    s_mulAddF32.o \
    s_normSubnormalF64Sig.o \
    s_roundPackToF64.o \
    s_normRoundPackToF64.o \
    s_addMagsF64.o \
    s_subMagsF64.o \
    s_mulAddF64.o \
    s_normSubnormalExtF80Sig.o \
    s_roundPackToExtF80.o \
    s_normRoundPackToExtF80.o \
    s_addMagsExtF80.o \
    s_subMagsExtF80.o \
    s_normSubnormalF128Sig.o \
    s_roundPackToF128.o \
    s_normRoundPackToF128.o \
    s_addMagsF128.o \
    s_subMagsF128.o \
    s_mulAddF128.o \
    softfloat_state.o \
    ui32_to_f16.o \
    ui32_to_f32.o \
    ui32_to_f64.o \
    ui32_to_extF80.o \
    ui32_to_extF80M.o \
    ui32_to_f128.o \
    ui32_to_f128M.o \
    ui64_to_f16.o \
    ui64_to_f32.o \
    ui64_to_f64.o \
    ui64_to_extF80.o \
    ui64_to_extF80M.o \
    ui64_to_f128.o \
    ui64_to_f128M.o \
    i32_to_f16.o \
    i32_to_f32.o \
    i32_to_f64.o \
    i32_to_extF80.o \
    i32_to_extF80M.o \
    i32_to_f128.o \
    i32_to_f128M.o \
    i64_to_f16.o \
    i64_to_f32.o \
    i64_to_f64.o \
    i64_to_extF80.o \
    i64_to_extF80M.o \
    i64_to_f128.o \
    i64_to_f128M.o \
    f16_to_ui32.o \
    f16_to_ui64.o \
    f16_to_i32.o \
    f16_to_i64.o \
    f16_to_ui32_r_minMag.o \
    f16_to_ui64_r_minMag.o \
    f16_to_i32_r_minMag.o \
    f16_to_i64_r_minMag.o \
    f16_to_f32.o \
    f16_to_f64.o \
    f16_to_extF80.o \
    f16_to_extF80M.o \
    f16_to_f128.o \
    f16_to_f128M.o \
    f16_roundToInt.o \
    f16_add.o \
    f16_sub.o \
    f16_mul.o \
    f16_mulAdd.o \
    f16_div.o \
    f16_rem.o \
    f16_sqrt.o \
    f16_eq.o \
    f16_le.o \
    f16_lt.o \
    f16_eq_signaling.o \
    f16_le_quiet.o \
    f16_lt_quiet.o \
    f16_isSignalingNaN.o \
    f32_to_ui32.o \
    f32_to_ui64.o \
    f32_to_i32.o \
    f32_to_i64.o \
    f32_to_ui32_r_minMag.o \
    f32_to_ui64_r_minMag.o \
    f32_to_i32_r_minMag.o \
    f32_to_i64_r_minMag.o \
    f32_to_f16.o \
    f32_to_f64.o \
    f32_to_extF80.o \
    f32_to_extF80M.o \
    f32_to_f128.o \
    f32_to_f128M.o \
    f32_roundToInt.o \
    f32_add.o \
    f32_sub.o \
    f32_mul.o \
    f32_mulAdd.o \
    f32_div.o \
    f32_rem.o \
    f32_sqrt.o \
    f32_eq.o \
    f32_le.o \
    f32_lt.o \
    f32_eq_signaling.o \
    f32_le_quiet.o \
    f32_lt_quiet.o \
    f32_isSignalingNaN.o \
    f64_to_ui32.o \
    f64_to_ui64.o \
    f64_to_i32.o \
    f64_to_i64.o \
    f64_to_ui32_r_minMag.o \
    f64_to_ui64_r_minMag.o \
    f64_to_i32_r_minMag.o \
    f64_to_i64_r_minMag.o \
    f64_to_f16.o \
    f64_to_f32.o \
    f64_to_extF80.o \
    f64_to_extF80M.o \
    f64_to_f128.o \
    f64_to_f128M.o \
    f64_roundToInt.o \
    f64_add.o \
    f64_sub.o \
    f64_mul.o \
    f64_mulAdd.o \
    f64_div.o \
    f64_rem.o \
    f64_sqrt.o \
    f64_eq.o \
    f64_le.o \
    f64_lt.o \
    f64_eq_signaling.o \
    f64_le_quiet.o \
    f64_lt_quiet.o \
    f64_isSignalingNaN.o \
    extF80_to_ui32.o \
    extF80_to_ui64.o \
    extF80_to_i32.o \
    extF80_to_i64.o \
    extF80_to_ui32_r_minMag.o \
    extF80_to_ui64_r_minMag.o \
    extF80_to_i32_r_minMag.o \
    extF80_to_i64_r_minMag.o \
    extF80_to_f16.o \
    extF80_to_f32.o \
    extF80_to_f64.o \
    extF80_to_f128.o \
    extF80_roundToInt.o \
    extF80_add.o \
    extF80_sub.o \
    extF80_mul.o \
    extF80_div.o \
    extF80_rem.o \
    extF80_sqrt.o \
    extF80_eq.o \
    extF80_le.o \
    extF80_lt.o \
    extF80_eq_signaling.o \
    extF80_le_quiet.o \
    extF80_lt_quiet.o \
    extF80_isSignalingNaN.o \
    extF80M_to_ui32.o \
    extF80M_to_ui64.o \
    extF80M_to_i32.o \
    extF80M_to_i64.o \
    extF80M_to_ui32_r_minMag.o \
    extF80M_to_ui64_r_minMag.o \
    extF80M_to_i32_r_minMag.o \
    extF80M_to_i64_r_minMag.o \
    extF80M_to_f16.o \
    extF80M_to_f32.o \
    extF80M_to_f64.o \
    extF80M_to_f128M.o \
    extF80M_roundToInt.o \
    extF80M_add.o \
    extF80M_sub.o \
    extF80M_mul.o \
    extF80M_div.o \
    extF80M_rem.o \
    extF80M_sqrt.o \
    extF80M_eq.o \
    extF80M_le.o \
    extF80M_lt.o \
    extF80M_eq_signaling.o \
    extF80M_le_quiet.o \
    extF80M_lt_quiet.o \
    f128_to_ui32.o \
    f128_to_ui64.o \
    f128_to_i32.o \
    f128_to_i64.o \
    f128_to_ui32_r_minMag.o \
    f128_to_ui64_r_minMag.o \
    f128_to_i32_r_minMag.o \
    f128_to_i64_r_minMag.o \
    f128_to_f16.o \
    f128_to_f32.o \
    f128_to_extF80.o \
    f128_to_f64.o \
    f128_roundToInt.o \
    f128_add.o \
    f128_sub.o \
    f128_mul.o \
    f128_mulAdd.o \
    f128_div.o \
    f128_rem.o \
    f128_sqrt.o \
    f128_eq.o \
    f128_le.o \
    f128_lt.o \
    f128_eq_signaling.o \
    f128_le_quiet.o \
    f128_lt_quiet.o \
    f128_isSignalingNaN.o \
    f128M_to_ui32.o \
    f128M_to_ui64.o \
    f128M_to_i32.o \
    f128M_to_i64.o \
    f128M_to_ui32_r_minMag.o \
    f128M_to_ui64_r_minMag.o \
    f128M_to_i32_r_minMag.o \
    f128M_to_i64_r_minMag.o \
    f128M_to_f16.o \
    f128M_to_f32.o \
    f128M_to_extF80M.o \
    f128M_to_f64.o \
    f128M_roundToInt.o \
    f128M_add.o \
    f128M_sub.o \
    f128M_mul.o \
    f128M_mulAdd.o \
    f128M_div.o \
    f128M_rem.o \
    f128M_sqrt.o \
    f128M_eq.o \
    f128M_le.o \
    f128M_lt.o \
    f128M_eq_signaling.o \
    f128M_le_quiet.o \
    f128M_lt_quiet.o

SOFTFLOAT_FILES := $(addprefix $(SOFTFLOAT_DIR)/, \
    $(SOFTFLOAT_OBJS_PRIMITIVES:.o=.c) \
    $(SOFTFLOAT_OBJS_SPECIALIZE:.o=.c) \
    $(SOFTFLOAT_OBJS_OTHERS:.o=.c))
SOFTFLOAT_OBJS := $(addprefix $(OUT)/softfloat/, \
    $(SOFTFLOAT_OBJS_PRIMITIVES) \
    $(SOFTFLOAT_OBJS_SPECIALIZE) \
    $(SOFTFLOAT_OBJS_OTHERS))

SOFTFLOAT_SENTINEL := src/softfloat/.git

$(SOFTFLOAT_SENTINEL):
	$(Q)git clone https://github.com/ucb-bar/berkeley-softfloat-3 $(dir $@) --depth=1
SOFTFLOAT_DUMMY_PLAT := $(OUT)/softfloat/platform.h
$(SOFTFLOAT_DUMMY_PLAT):
	$(Q)mkdir -p $(shell dirname $@)
	$(Q)touch $@

$(SOFTFLOAT_FILES): $(SOFTFLOAT_SENTINEL)
$(OUT)/softfloat/%.o: $(SOFTFLOAT_DIR)/%.c $(SOFTFLOAT_SENTINEL) $(SOFTFLOAT_DUMMY_PLAT)
	$(Q)mkdir -p $(shell dirname $@)
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS) $(CFLAGS_softfloat) -c -MMD -MF $@.d $<

SOFTFLOAT_LIB := $(OUT)/softfloat/softfloat.a
$(SOFTFLOAT_LIB): $(SOFTFLOAT_OBJS)
	$(VECHO) "  AR\t$@\n"
	$(Q)$(AR) crs $@ $(SOFTFLOAT_OBJS)
