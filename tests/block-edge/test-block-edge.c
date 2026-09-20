/* Verify predecessor ownership independently of cache replacement order. */

#include <assert.h>
#include <string.h>

#include "riscv_private.h"

#if RV32_HAS(JIT) && RV32_HAS(BLOCK_CHAINING)
static void init_block(block_t *block, rv_insn_t *ir)
{
    memset(block, 0, sizeof(*block));
    memset(ir, 0, sizeof(*ir));
    block->ir_head = ir;
    block->ir_tail = ir;
    block_init_edge_lists(block);
}

int main(void)
{
    block_t source_a, source_b, target;
    rv_insn_t ir_a, ir_b, ir_target;

    init_block(&source_a, &ir_a);
    init_block(&source_b, &ir_b);
    init_block(&target, &ir_target);
    source_a.pc_start = 4;
    source_b.pc_start = 8;
    target.pc_start = 12;

    block_link_edge(&source_a, true, &target);
    block_link_edge(&source_b, false, &target);
    assert(block_incoming_edge_count(&target) == 2);

    /* Target eviction must clear both still-live predecessor slots. */
    block_unlink_incoming_edges(&target);
    assert(!ir_a.branch_taken);
    assert(!ir_b.branch_untaken);
    assert(block_incoming_edge_count(&target) == 0);

    /* A compiling source retains its outgoing edges after eviction. A new
     * predecessor may arrive before deferred cleanup unlinks the old source.
     */
    block_link_edge(&source_a, true, &target);
    block_link_edge(&source_a, false, &source_a);
    block_unlink_incoming_edges(&source_a);
    assert(ir_a.branch_untaken == &ir_a);
    block_link_edge(&source_b, true, &target);
    assert(block_incoming_edge_count(&target) == 2);
    block_unlink_outgoing_edges(&source_a);
    assert(!ir_a.branch_taken && !ir_a.branch_untaken);
    assert(ir_b.branch_taken == &ir_target);
    assert(block_incoming_edge_count(&target) == 1);

    /* The target can instead be evicted before deferred source cleanup. */
    block_link_edge(&source_a, true, &target);
    block_unlink_incoming_edges(&source_a);
    block_unlink_edges(&target);
    block_unlink_outgoing_edges(&source_a);
    assert(!ir_a.branch_taken && !ir_b.branch_taken);
    assert(block_incoming_edge_count(&target) == 0);

    block_link_edge(&target, true, &target);
    assert(block_incoming_edge_count(&target) == 1);
    block_unlink_edges(&target);
    assert(!ir_target.branch_taken);
    assert(block_incoming_edge_count(&target) == 0);

    /* Lazy macro-op fusion runs on blocks that are already chained, so a
     * source can retire the very instruction that carried its link. The edge
     * has to follow the new tail; resolving it against the retired record
     * would clear a pool slot that another block already owns.
     */
    block_t shrinking;
    rv_insn_t ir_pair[2];

    memset(&shrinking, 0, sizeof(shrinking));
    memset(ir_pair, 0, sizeof(ir_pair));
    shrinking.ir_head = &ir_pair[0];
    shrinking.ir_tail = &ir_pair[1];
    block_init_edge_lists(&shrinking);
    shrinking.pc_start = 16;

    block_link_edge(&shrinking, true, &target);
    assert(block_incoming_edge_count(&target) == 1);

    /* What remove_next_nth_ir() does once the retired run reaches the tail. */
    ir_pair[0].branch_taken = ir_pair[1].branch_taken;
    ir_pair[0].branch_untaken = ir_pair[1].branch_untaken;
    shrinking.ir_tail = &ir_pair[0];
    memset(&ir_pair[1], 0, sizeof(ir_pair[1])); /* the pool slot is reused */

    block_unlink_incoming_edges(&target);
    assert(!ir_pair[0].branch_taken);
    assert(!ir_pair[1].branch_taken);
    assert(block_incoming_edge_count(&target) == 0);
    return 0;
}
#else
int main(void)
{
    return 0;
}
#endif
