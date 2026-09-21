/* LR.W and SC.W reservation set.
 *
 * SC.W used to store and report success unconditionally, because no
 * reservation was ever registered. Every store conditional therefore
 * succeeded: one with no preceding LR.W, one naming a different address, and
 * a second one after the reservation had already been consumed. A guest
 * building a lock out of those could take it twice.
 *
 * Each case checks the word in memory as well as the returned code, so an
 * SC.W that reports failure while still performing its store is caught too.
 */
    .option norvc
    .text
    .global _start

_start:
    la   s0, word_a
    la   s1, word_b

    /* An SC.W with no reservation fails and leaves memory alone. */
    li   t1, 0xaaaaaaaa
    sc.w t0, t1, (s0)
    beqz t0, fail
    lw   t2, 0(s0)
    bnez t2, fail

    /* LR.W then SC.W on the same address is the paired case, and the one
     * sequence that has to succeed or no guest makes progress.
     */
    lr.w t2, (s0)
    li   t1, 0x11112222
    sc.w t0, t1, (s0)
    bnez t0, fail
    lw   t2, 0(s0)
    li   t3, 0x11112222
    bne  t2, t3, fail

    /* That SC.W consumed the reservation, so the next one fails even
     * though the address still matches.
     */
    li   t1, 0x33334444
    sc.w t0, t1, (s0)
    beqz t0, fail
    lw   t2, 0(s0)
    li   t3, 0x11112222
    bne  t2, t3, fail

    /* An SC.W naming an address other than the reserved one fails. */
    lr.w t2, (s0)
    li   t1, 0x55556666
    sc.w t0, t1, (s1)
    beqz t0, fail
    lw   t2, 0(s1)
    bnez t2, fail

    /* Only the most recent LR.W counts, so reserving word_b leaves word_a
     * unreserved.
     */
    lr.w t2, (s0)
    lr.w t2, (s1)
    li   t1, 0x9999aaaa
    sc.w t0, t1, (s0)
    beqz t0, fail
    lw   t2, 0(s0)
    li   t3, 0x11112222
    bne  t2, t3, fail

    /* A trap between LR.W and SC.W discards the reservation, which is what
     * keeps one task's reservation from being honoured after a context
     * switch. A zero-length write is the cheapest trap reachable here.
     */
    lr.w t2, (s0)
    li   a0, 1
    la   a1, passed
    li   a2, 0
    li   a7, 64
    ecall
    li   t1, 0xdeadbeef
    sc.w t0, t1, (s0)
    beqz t0, fail
    lw   t2, 0(s0)
    li   t3, 0x11112222
    bne  t2, t3, fail

    /* LR.W with rd=x0 still performs the load, so it still reserves. The
     * discarded result does not make the access go away: it can still
     * fault, and on an MMIO address it still has side effects.
     */
    lr.w zero, (s0)
    li   t1, 0x0c0ffee0
    sc.w t0, t1, (s0)
    bnez t0, fail
    lw   t2, 0(s0)
    li   t3, 0x0c0ffee0
    bne  t2, t3, fail

    /* A failing SC.W reports through rd, which must stay zero when rd is
     * x0 rather than having the failure code written into it.
     */
    li   t1, 0x5
    sc.w zero, t1, (s0)
    bnez zero, fail
    lw   t2, 0(s0)
    li   t3, 0x0c0ffee0
    bne  t2, t3, fail

    li   a0, 1
    la   a1, passed
    li   a2, 21
    li   a7, 64
    ecall
    li   a0, 0
    j    exit
fail:
    li   a0, 1
exit:
    li   a7, 93
    ecall

    .section .rodata
passed:
    .ascii "LR/SC reservation OK\n"

    .section .data
    .balign 4
word_a:
    .word 0
word_b:
    .word 0
