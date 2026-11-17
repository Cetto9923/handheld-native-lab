.text
#ifdef __APPLE__
.globl _ffrs_r3_invoke
_ffrs_r3_invoke:
#else
.globl ffrs_r3_invoke
ffrs_r3_invoke:
#endif
    stp x29, x30, [sp, #-16]!
    stp d9, d10, [sp, #-16]!
    mov x9, x0
    fmov s9, s0
    fmov s10, s1
    mov x16, #0x1234
    mov x17, #0x5678
    msr nzcv, x1
    mrs x12, nzcv
    str x12, [x2, #8]
    mov x12, sp
    str x12, [x2, #24]
    blr x9
    mrs x12, nzcv
    str x12, [x2, #16]
    mov x12, sp
    str x12, [x2, #32]
    str x16, [x2, #40]
    str x17, [x2, #48]
    str s0, [x2]
    ldp d9, d10, [sp], #16
    ldp x29, x30, [sp], #16
    ret
