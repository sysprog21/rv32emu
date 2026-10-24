/* ACT exit reporting must not truncate a failing test number to shell
 * success.
 */
.global _start, tohost, begin_signature, end_signature
_start:
    la t0, tohost
    li t1, TEST_RESULT
    sw t1, 0(t0)
1:  j 1b

.data
.balign 8
tohost: .word 0, 0
begin_signature: .word 0
end_signature:
