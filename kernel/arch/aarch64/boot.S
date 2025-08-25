.globl _start
_start:
    mov x20, sp
    msr spsel, #1
    mov sp, x20
    bl kmain
    b .