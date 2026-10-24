/* Architectural permission/effective-privilege cases, including MPRV, which
 * the flat S-mode kernel cannot enter architecturally without M firmware.
 */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>

#include "system.h"

int main(void)
{
    riscv_t rv = {0};
    rv.csr_satp = UINT32_C(0x80000000);
    rv.priv_mode = RV_PRIV_U_MODE;
    assert(!mmu_perm_ok(&rv, PTE_R | PTE_W, PTE_R));
    assert(!mmu_perm_ok(&rv, PTE_R | PTE_W, PTE_W));
    assert(mmu_perm_ok(&rv, PTE_R | PTE_W | PTE_U, PTE_R));
    assert(mmu_perm_ok(&rv, PTE_R | PTE_W | PTE_U, PTE_W));
    assert(!mmu_perm_ok(&rv, PTE_X | PTE_U, PTE_R));
    const uint32_t user_context = mmu_access_context_value(&rv);
    rv.csr_sstatus = SSTATUS_SUM;
    assert(mmu_access_context_value(&rv) == user_context); /* SUM irrelevant */
    rv.csr_sstatus |= SSTATUS_MXR;
    assert(mmu_perm_ok(&rv, PTE_X | PTE_U, PTE_R));
    assert(!mmu_perm_ok(&rv, PTE_X | PTE_U, PTE_W));
    assert(mmu_access_context_value(&rv) != user_context);

    rv.priv_mode = RV_PRIV_S_MODE;
    rv.csr_sstatus = 0;
    const uint32_t supervisor_context = mmu_access_context_value(&rv);
    assert(!mmu_perm_ok(&rv, PTE_R | PTE_W | PTE_U, PTE_R));
    assert(!mmu_perm_ok(&rv, PTE_R | PTE_W | PTE_U, PTE_W));
    assert(!mmu_perm_ok(&rv, PTE_X | PTE_U, PTE_X));
    rv.csr_sstatus = SSTATUS_SUM;
    /* Kernel stamps survive SUM; U pages are parked when it clears. */
    assert(mmu_access_context_value(&rv) == supervisor_context);
    assert(mmu_perm_ok(&rv, PTE_R | PTE_W | PTE_U, PTE_R));
    assert(mmu_perm_ok(&rv, PTE_R | PTE_W | PTE_U, PTE_W));
    assert(!mmu_perm_ok(&rv, PTE_X | PTE_U, PTE_X));
    assert(mmu_perm_ok(&rv, PTE_X, PTE_X));
    rv.csr_sstatus |= SSTATUS_MXR;
    assert(mmu_perm_ok(&rv, PTE_X, PTE_R));
    assert(!mmu_perm_ok(&rv, PTE_X, PTE_W));

    rv.priv_mode = RV_PRIV_M_MODE;
    const uint32_t machine_context = mmu_access_context_value(&rv);
    assert(mmu_data_priv(&rv) == RV_PRIV_M_MODE);
    assert(!mmu_paging(&rv, mmu_data_priv(&rv)));
    rv.csr_sstatus = 0;
    assert(mmu_access_context_value(&rv) == machine_context);
    rv.csr_mstatus = MSTATUS_MPRV | (RV_PRIV_S_MODE << MSTATUS_MPP_SHIFT);
    assert(mmu_data_priv(&rv) == RV_PRIV_S_MODE);
    assert(mmu_paging(&rv, mmu_data_priv(&rv)));
    assert(!mmu_perm_ok(&rv, PTE_R | PTE_U, PTE_R));
    rv.csr_sstatus = SSTATUS_SUM;
    assert(mmu_perm_ok(&rv, PTE_R | PTE_U, PTE_R));
    rv.csr_mstatus = MSTATUS_MPRV; /* MPP=U */
    assert(mmu_data_priv(&rv) == RV_PRIV_U_MODE);
    rv.mmu_access_context = mmu_access_context_value(&rv);
    assert(rv.mmu_access_context != 0);
    assert(mmu_ram_tag_context(&rv) == rv.mmu_access_context);
    rv.priv_mode = RV_PRIV_U_MODE;
    assert(mmu_ram_tag_context(&rv) == 0); /* same effective U, actual U */
    rv.priv_mode = RV_PRIV_M_MODE;
    assert(!mmu_perm_ok(&rv, PTE_R, PTE_R));
    assert(mmu_perm_ok(&rv, PTE_R | PTE_U, PTE_R));
    assert(!mmu_paging(&rv, rv.priv_mode)); /* fetch ignores MPRV */
    rv.csr_satp = 1; /* MODE=Bare even with nonzero PPN */
    assert(!mmu_paging(&rv, mmu_data_priv(&rv)));
    puts("MMU permission contexts OK");
    return 0;
}
