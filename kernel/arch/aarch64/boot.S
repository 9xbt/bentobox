.globl _start
_start:
    mov x20, sp
    msr spsel, #1
    mov sp, x20
    bl kmain
    b .

.globl _ap_trampoline
_ap_trampoline:
    mov x20, sp
    msr spsel, #1
    mov sp, x20
    bl ap_startup
    b .

.global __aarch64_swp1_acq
__aarch64_swp1_acq:
1:  ldaxrb  w2, [x1]
    stlxrb  w3, w0, [x1]
    cbnz    w3, 1b
    mov     x0, x2
    ret