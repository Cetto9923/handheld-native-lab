/* SPDX-License-Identifier: MIT
 * No C/TLS helper is called with a guest x18. No TP register is modified.
 */
#if defined(__ANDROID__) && defined(__aarch64__)
.text
.p2align 2
.global nb_call
.type nb_call,%function
nb_call:
    .inst 0xd503245f /* bti c */
    cbz x0, .Linvalid
    cbz x1, .Linvalid
    cbz x2, .Linvalid
    tst x0, #7
    b.ne .Linvalid
    tst x2, #15
    b.ne .Linvalid
    ldr x9, [x0]
    mov x10, #0x4e42
    cmp x9, x10
    b.ne .Linvalid
    ldr x10,[x0,#40]
    cbz x10,.Linvalid
    ldar x9,[x10]
    cbz x9,.Lowner_dead
    mrs x9, tpidr_el0
    ldr x10, [x0, #8]
    cmp x9, x10
    b.ne .Lwrong_thread
    add x11, x0, #24
.Lclaim:
    ldaxr x9, [x11]
    cbnz x9, .Lbusy
    mov x10, #1
    stlxr w9, x10, [x11]
    cbnz w9, .Lclaim
    str x18, [x0, #16] /* capture exact host SCS depth at assembly entry */
    sub sp, sp, #256
    stp x29, x30, [sp]
    stp x19, x20, [sp, #16]
    stp x21, x22, [sp, #32]
    stp x23, x24, [sp, #48]
    stp x25, x26, [sp, #64]
    stp x27, x28, [sp, #80]
    stp q8, q9, [sp, #96]
    stp q10, q11, [sp, #128]
    stp q12, q13, [sp, #160]
    stp q14, q15, [sp, #192]
    stp x0, x2, [sp, #224]
    str x18, [sp, #240]
    mov x29, sp
    mov x16, x1
    mov x17, x2
    ldr x18, [x17, #72]
    ldp q0, q1, [x17, #80]
    ldp q2, q3, [x17, #112]
    ldp q4, q5, [x17, #144]
    ldp q6, q7, [x17, #176]
    ldp x0, x1, [x17]
    ldp x2, x3, [x17, #16]
    ldp x4, x5, [x17, #32]
    ldp x6, x7, [x17, #48]
    ldr x8, [x17, #64]
    blr x16
    ldp x16, x17, [sp, #224]
    stp x0, x1, [x17]
    stp x2, x3, [x17, #16]
    stp x4, x5, [x17, #32]
    stp x6, x7, [x17, #48]
    str x8, [x17, #64]
    str x18, [x17, #72]
    stp q0, q1, [x17, #80]
    stp q2, q3, [x17, #112]
    stp q4, q5, [x17, #144]
    stp q6, q7, [x17, #176]
    ldr x18, [sp, #240]
    str xzr, [x16, #16]
    add x9, x16, #24
    stlr xzr, [x9]
    ldp q8, q9, [sp, #96]
    ldp q10, q11, [sp, #128]
    ldp q12, q13, [sp, #160]
    ldp q14, q15, [sp, #192]
    ldp x19, x20, [sp, #16]
    ldp x21, x22, [sp, #32]
    ldp x23, x24, [sp, #48]
    ldp x25, x26, [sp, #64]
    ldp x27, x28, [sp, #80]
    ldp x29, x30, [sp]
    add sp, sp, #256
    mov w0, #0
    ret
.Lbusy:
    clrex
    mov w0, #4
    ret
.Lwrong_thread:
    mov w0, #3
    ret
.Lowner_dead:
    mov w0,#7
    ret
.Linvalid:
    mov w0, #2
    ret
.size nb_call,.-nb_call

.p2align 2
.global nb_import_gateway
.hidden nb_import_gateway
.type nb_import_gateway,%function
nb_import_gateway:
    .inst 0xd50324df /* bti jc: thunk uses an indirect BR */
    ldr x9, [x16]
    mov x10, #0x4e42
    cmp x9, x10
    b.ne .Ltrap
    ldr x10,[x16,#40]
    cbz x10,.Ltrap
    ldar x9,[x10]
    cbz x9,.Ltrap
    mrs x9, tpidr_el0
    ldr x10, [x16, #8]
    cmp x9, x10
    b.ne .Ltrap
    add x11, x16, #24
    ldar x9, [x11]
    cmp x9, #1
    b.ne .Ltrap
    stp x29, x30, [sp, #-32]!
    stp x18, x16, [sp, #16]
    mov x29, sp
    mov x9, #2
    stlr x9, [x11]
    ldr x18, [x16, #16] /* restore before the first possible host SCS prologue */
    blr x17
    ldr x16, [sp, #24]
    add x11, x16, #24
    mov x9, #1
    stlr x9, [x11]
    ldr x18, [sp, #16]
    ldp x29, x30, [sp], #32
    ret
.Ltrap:
    brk #0x4e42
.size nb_import_gateway,.-nb_import_gateway
.section .note.GNU-stack,"",%progbits
#endif
