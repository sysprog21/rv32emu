
#define RVMODEL_HALT \
    add a7, x0, 93;  \
    add a0, x0, 0;   \
    ecall

#define RVMODEL_BOOT

#define RVTEST_RV32M

#define RVMODEL_DATA_BEGIN   \
    .align 4;                \
    .global begin_signature; \
    begin_signature:

#define RVMODEL_DATA_END   \
    .align 4;              \
    .global end_signature; \
    end_signature:

#define RVMODEL_IO_INIT
#define RVMODEL_IO_WRITE_STR(_SP, _STR)
#define RVMODEL_IO_CHECK()
#define RVMODEL_IO_ASSERT_GPR_EQ(_SP, _R, _I)
#define RVMODEL_IO_ASSERT_SFPR_EQ(_F, _R, _I)
#define RVMODEL_IO_ASSERT_DFPR_EQ(_D, _R, _I)
#define RVMODEL_SET_MSW_INT
#define RVMODEL_CLEAR_MSW_INT
#define RVMODEL_CLEAR_MTIMER_INT
#define RVMODEL_CLEAR_MEXT_INT
