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
    stp x29, x30, [sp, #-48]!
    stp x18, x16, [sp, #16]
    ldr x9,[x16,#48]
    str x9,[sp,#32]
    str x18,[x16,#48] /* actual guest x18 before restoring host SCS */
    mov x29, sp
    mov x9, #2
    stlr x9, [x11]
    ldr x18, [x16, #16] /* restore before the first possible host SCS prologue */
    blr x17
    ldr x16, [sp, #24]
    ldr x9,[sp,#32]
    str x9,[x16,#48]
    add x11, x16, #24
    mov x9, #1
    stlr x9, [x11]
    ldr x18, [sp, #16]
    ldp x29, x30, [sp], #48
    ret
.Ltrap:
    brk #0x4e42
.size nb_import_gateway,.-nb_import_gateway

/* Exactly 104 bytes; x9/x10 are scratch, never argument/result registers. */
.macro NB_COPY_STACK13 source,destination
    ldp x9,x10,[\source]
    stp x9,x10,[\destination]
    ldp x9,x10,[\source,#16]
    stp x9,x10,[\destination,#16]
    ldp x9,x10,[\source,#32]
    stp x9,x10,[\destination,#32]
    ldp x9,x10,[\source,#48]
    stp x9,x10,[\destination,#48]
    ldp x9,x10,[\source,#64]
    stp x9,x10,[\destination,#64]
    ldp x9,x10,[\source,#80]
    stp x9,x10,[\destination,#80]
    ldr x9,[\source,#96]
    str x9,[\destination,#96]
.endm

.p2align 2
.global nb_call_stack13
.type nb_call_stack13,%function
nb_call_stack13:
    .inst 0xd503245f
    cbz x0,.Lst_invalid
    cbz x1,.Lst_invalid
    cbz x2,.Lst_invalid
    cbz x3,.Lst_invalid
    tst x0,#7
    b.ne .Lst_invalid
    tst x2,#15
    b.ne .Lst_invalid
    tst x3,#7
    b.ne .Lst_invalid
    ldr x9,[x0]
    mov x10,#0x4e42
    cmp x9,x10
    b.ne .Lst_invalid
    ldr x10,[x0,#40]
    cbz x10,.Lst_invalid
    ldar x9,[x10]
    cbz x9,.Lst_dead
    mrs x9,tpidr_el0
    ldr x10,[x0,#8]
    cmp x9,x10
    b.ne .Lst_thread
    add x11,x0,#24
.Lst_claim:
    ldaxr x9,[x11]
    cbnz x9,.Lst_busy
    mov x10,#1
    stlxr w9,x10,[x11]
    cbnz w9,.Lst_claim
    str x18,[x0,#16]
    sub sp,sp,#368 /* outgoing 112 + original save area 256 */
    stp x29,x30,[sp,#112]
    stp x19,x20,[sp,#128]
    stp x21,x22,[sp,#144]
    stp x23,x24,[sp,#160]
    stp x25,x26,[sp,#176]
    stp x27,x28,[sp,#192]
    stp q8,q9,[sp,#208]
    stp q10,q11,[sp,#240]
    stp q12,q13,[sp,#272]
    stp q14,q15,[sp,#304]
    stp x0,x2,[sp,#336]
    str x18,[sp,#352]
    add x29,sp,#112
    NB_COPY_STACK13 x3,sp
    str xzr,[sp,#104] /* alignment padding, not another argument */
    mov x16,x1
    mov x17,x2
    ldr x18,[x17,#72]
    ldp q0,q1,[x17,#80]
    ldp q2,q3,[x17,#112]
    ldp q4,q5,[x17,#144]
    ldp q6,q7,[x17,#176]
    ldp x0,x1,[x17]
    ldp x2,x3,[x17,#16]
    ldp x4,x5,[x17,#32]
    ldp x6,x7,[x17,#48]
    ldr x8,[x17,#64]
    blr x16
    ldp x16,x17,[sp,#336]
    stp x0,x1,[x17]
    stp x2,x3,[x17,#16]
    stp x4,x5,[x17,#32]
    stp x6,x7,[x17,#48]
    str x8,[x17,#64]
    str x18,[x17,#72]
    stp q0,q1,[x17,#80]
    stp q2,q3,[x17,#112]
    stp q4,q5,[x17,#144]
    stp q6,q7,[x17,#176]
    ldr x18,[sp,#352]
    str xzr,[x16,#16]
    add x9,x16,#24
    stlr xzr,[x9]
    ldp q8,q9,[sp,#208]
    ldp q10,q11,[sp,#240]
    ldp q12,q13,[sp,#272]
    ldp q14,q15,[sp,#304]
    ldp x19,x20,[sp,#128]
    ldp x21,x22,[sp,#144]
    ldp x23,x24,[sp,#160]
    ldp x25,x26,[sp,#176]
    ldp x27,x28,[sp,#192]
    ldp x29,x30,[sp,#112]
    add sp,sp,#368
    mov w0,#0
    ret
.Lst_busy:
    clrex
    mov w0,#4
    ret
.Lst_thread:
    mov w0,#3
    ret
.Lst_dead:
    mov w0,#7
    ret
.Lst_invalid:
    mov w0,#2
    ret
.size nb_call_stack13,.-nb_call_stack13

.p2align 2
.global nb_import_stack13_gateway
.hidden nb_import_stack13_gateway
.type nb_import_stack13_gateway,%function
nb_import_stack13_gateway:
    .inst 0xd50324df
    ldr x9,[x16]
    mov x10,#0x4e42
    cmp x9,x10
    b.ne .Lst_trap
    ldr x10,[x16,#40]
    cbz x10,.Lst_trap
    ldar x9,[x10]
    cbz x9,.Lst_trap
    mrs x9,tpidr_el0
    ldr x10,[x16,#8]
    cmp x9,x10
    b.ne .Lst_trap
    add x11,x16,#24
    ldar x9,[x11]
    cmp x9,#1
    b.ne .Lst_trap
    sub sp,sp,#160 /* outgoing 112 + gateway metadata 48 */
    stp x29,x30,[sp,#112]
    stp x18,x16,[sp,#128]
    ldr x9,[x16,#48]
    str x9,[sp,#144]
    str x18,[x16,#48]
    add x29,sp,#112
    add x12,sp,#160 /* ORIGINAL incoming guest SP, before this frame */
    NB_COPY_STACK13 x12,sp
    str xzr,[sp,#104]
    mov x9,#2
    stlr x9,[x11]
    ldr x18,[x16,#16]
    blr x17
    ldr x16,[sp,#136]
    ldr x9,[sp,#144]
    str x9,[x16,#48]
    add x11,x16,#24
    mov x9,#1
    stlr x9,[x11]
    ldr x18,[sp,#128]
    ldp x29,x30,[sp,#112]
    add sp,sp,#160
    ret
.Lst_trap:
    brk #0x4e42
.size nb_import_stack13_gateway,.-nb_import_stack13_gateway

.p2align 2
.global nb_import_snprintf_gateway
.hidden nb_import_snprintf_gateway
.type nb_import_snprintf_gateway,%function
nb_import_snprintf_gateway:
    .inst 0xd50324df /* bti jc: thunk uses an indirect BR */
    ldr x9, [x16]
    mov x10, #0x4e42
    cmp x9, x10
    b.ne .Lsnp_trap
    ldr x10, [x16, #40]
    cbz x10, .Lsnp_trap
    ldar x9, [x10]
    cbz x9, .Lsnp_trap
    mrs x9, tpidr_el0
    ldr x10, [x16, #8]
    cmp x9, x10
    b.ne .Lsnp_trap
    add x11, x16, #24
    ldar x9, [x11]
    cmp x9, #1
    b.ne .Lsnp_trap
    mov x12, sp                     /* original incoming guest SP */
    sub sp, sp, #256                /* allocate 256-byte frame */
    stp q0, q1, [sp]                /* FP save area: q0-q7 at sp+0..sp+128 */
    stp q2, q3, [sp, #32]
    stp q4, q5, [sp, #64]
    stp q6, q7, [sp, #96]
    stp x3, x4, [sp, #128]          /* GP save area: x3-x7 at sp+128..sp+168 */
    stp x5, x6, [sp, #144]
    str x7, [sp, #160]
    /* AAPCS64 va_list structure at sp+176 (32 bytes): */
    str x12, [sp, #176]             /* __stack = original incoming guest SP */
    add x9, sp, #168
    str x9, [sp, #184]              /* __gr_top = sp + 168 (end of GP save area) */
    add x9, sp, #128
    str x9, [sp, #192]              /* __vr_top = sp + 128 (end of FP save area) */
    mov w9, #-40
    str w9, [sp, #200]              /* __gr_offs = -40 (5 GP args: x3..x7) */
    mov w9, #-128
    str w9, [sp, #204]              /* __vr_offs = -128 (8 FP args: q0..q7) */
    /* Gateway state save area at sp+208 (48 bytes): */
    stp x29, x30, [sp, #208]
    stp x18, x16, [sp, #224]
    ldr x9, [x16, #48]
    str x9, [sp, #240]              /* previous active_guest_x18 */
    str x18, [x16, #48]             /* actual guest x18 before restoring host SCS */
    add x29, sp, #208
    add x3, sp, #176                /* arg3: pointer to va_list */
    mov x9, #2
    stlr x9, [x11]                  /* phase = HOST */
    ldr x18, [x16, #16]             /* restore host SCS x18 */
    blr x17                         /* call vsnprintf(buf, size, fmt, ap) */
    ldr x16, [sp, #232]             /* restore scope pointer */
    ldr x9, [sp, #240]              /* restore previous active_guest_x18 */
    str x9, [x16, #48]
    add x11, x16, #24
    mov x9, #1
    stlr x9, [x11]                  /* phase = GUEST */
    ldr x18, [sp, #224]             /* restore guest x18 */
    ldp x29, x30, [sp, #208]
    add sp, sp, #256
    ret
.Lsnp_trap:
    brk #0x4e42
.size nb_import_snprintf_gateway,.-nb_import_snprintf_gateway

/* Owned HOST callback, register-only. Existing caller's HOST fields are stacked
 * and restored, not guessed or shared with a process-global host x18. */
.p2align 2
.global nb_call_from_host
.type nb_call_from_host,%function
nb_call_from_host:
    .inst 0xd503245f
    cbz x0,.Lcb_invalid
    cbz x1,.Lcb_invalid
    cbz x2,.Lcb_invalid
    tst x0,#7
    b.ne .Lcb_invalid
    tst x2,#15
    b.ne .Lcb_invalid
    ldr x9,[x0]
    mov x10,#0x4e42
    cmp x9,x10
    b.ne .Lcb_invalid
    ldr x10,[x0,#40]
    cbz x10,.Lcb_invalid
    ldar x9,[x10]
    cbz x9,.Lcb_dead
    mrs x9,tpidr_el0
    ldr x10,[x0,#8]
    cmp x9,x10
    b.ne .Lcb_thread
    add x11,x0,#24
    ldar x9,[x11]
    cmp x9,#2
    b.ne .Lcb_busy
    ldr x9,[x0,#56]
    cmp x9,#4
    b.hs .Lcb_depth
.Lcb_claim:
    ldaxr x9,[x11]
    cmp x9,#2
    b.ne .Lcb_busy
    mov x10,#1
    stlxr w9,x10,[x11]
    cbnz w9,.Lcb_claim
    sub sp,sp,#272
    stp x29,x30,[sp]
    stp x19,x20,[sp,#16]
    stp x21,x22,[sp,#32]
    stp x23,x24,[sp,#48]
    stp x25,x26,[sp,#64]
    stp x27,x28,[sp,#80]
    stp q8,q9,[sp,#96]
    stp q10,q11,[sp,#128]
    stp q12,q13,[sp,#160]
    stp q14,q15,[sp,#192]
    stp x0,x2,[sp,#224]
    ldr x9,[x0,#16]
    stp x18,x9,[sp,#240] /* CURRENT host SCS and outer scope host_x18 */
    ldp x9,x10,[x0,#48]
    stp x9,x10,[sp,#256] /* actual outer guest x18 and callback depth */
    str x18,[x0,#16]
    add x10,x10,#1
    str x10,[x0,#56]
    mov x29,sp
    mov x16,x1
    mov x17,x2
    mov x18,x9 /* actual guest suspended at this HOST import */
    ldp q0,q1,[x17,#80]
    ldp q2,q3,[x17,#112]
    ldp q4,q5,[x17,#144]
    ldp q6,q7,[x17,#176]
    ldp x0,x1,[x17]
    ldp x2,x3,[x17,#16]
    ldp x4,x5,[x17,#32]
    ldp x6,x7,[x17,#48]
    ldr x8,[x17,#64]
    blr x16
    ldp x16,x17,[sp,#224]
    stp x0,x1,[x17]
    stp x2,x3,[x17,#16]
    stp x4,x5,[x17,#32]
    stp x6,x7,[x17,#48]
    str x8,[x17,#64]
    str x18,[x17,#72]
    stp q0,q1,[x17,#80]
    stp q2,q3,[x17,#112]
    stp q4,q5,[x17,#144]
    stp q6,q7,[x17,#176]
    ldp x18,x9,[sp,#240]
    str x9,[x16,#16]
    ldp x9,x10,[sp,#256]
    stp x9,x10,[x16,#48]
    mov x10,#2
    add x9,x16,#24
    stlr x10,[x9]
    ldp q8,q9,[sp,#96]
    ldp q10,q11,[sp,#128]
    ldp q12,q13,[sp,#160]
    ldp q14,q15,[sp,#192]
    ldp x19,x20,[sp,#16]
    ldp x21,x22,[sp,#32]
    ldp x23,x24,[sp,#48]
    ldp x25,x26,[sp,#64]
    ldp x27,x28,[sp,#80]
    ldp x29,x30,[sp]
    add sp,sp,#272
    mov w0,#0
    ret
.Lcb_depth:
    mov w0,#10
    ret
.Lcb_busy:
    clrex
    mov w0,#4
    ret
.Lcb_thread:
    mov w0,#3
    ret
.Lcb_dead:
    mov w0,#7
    ret
.Lcb_invalid:
    mov w0,#2
    ret
.size nb_call_from_host,.-nb_call_from_host
.section .note.GNU-stack,"",%progbits
#endif
