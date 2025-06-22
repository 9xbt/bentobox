[BITS 32]

MB2_MAGIC    equ 0xE85250D6
MB2_ARCH     equ 0
MB2_LENGTH   equ 48
MB2_CHECKSUM equ -(MB2_MAGIC + MB2_ARCH + MB2_LENGTH)
MB2_TAG_TYPE_END equ 0
MB2_TAG_SIZE_END equ 8
MB2_TAG_TYPE_FRAMEBUFFER equ 5
MB2_TAG_FRAMEBUFFER_SIZE equ 20

section .multiboot

dd MB2_MAGIC
dd MB2_ARCH
dd MB2_LENGTH
dd MB2_CHECKSUM
align 8
dd MB2_TAG_TYPE_FRAMEBUFFER
dd MB2_TAG_FRAMEBUFFER_SIZE
dd 0
dd 0
dd 32
align 8
dd MB2_TAG_TYPE_END
dd MB2_TAG_SIZE_END

section .text.prologue
    global _start
    global gdtr
    global stack_top
    extern kmain
    extern initial_pml

_start:
    cli
    mov esp, stack_top

    push 0
    push eax
    push 0
    push ebx

    mov edi, initial_pml
    mov eax, initial_pml + 0x1000
    or eax, 0x7    ; PRESENT, WRITABLE, USER
    mov [edi], eax

    mov edi, initial_pml + 0x1000
    mov eax, initial_pml + 0x2000
    or eax, 0x7    ; PRESENT, WRITABLE, USER
    mov [edi], eax

    mov edi, initial_pml + 0x2000
    mov eax, 0x83  ; PRESENT, WRITABLE, HUGE
    mov ecx, 32
.set_entry:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    loop .set_entry

    mov eax, cr4
    or eax, 1 << 5  ; PAE bit
    mov cr4, eax

    mov eax, initial_pml
    mov cr3, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x100   ; LME bit
    wrmsr

    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    lgdt [gdtr]

    jmp 0x08:gdt2

[BITS 64]

gdt2:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov rax, cr0
    and ax, 0xFFFB  ; CR0.EM
    or ax, 0x2      ; CR0.MP
    mov cr0, rax
    mov rax, cr4
    or ax, 3 << 9   ; CR4.OSFXSR and CR4.OSXMMEXCPT
    mov cr4, rax

    pop rdi
    pop rsi
    call kmain

hltLoop:
    cli
    hlt
    jmp hltLoop

gdtr:
    dw gdt_end - gdt_base - 1
    dq gdt_base
gdt_base:
    ; null segment
    dq 0
    ; 64-bit segment
    dw 0
    dw 0
    db 0
    db 0x9a
    db 0x20
    db 0
    ; 64-bit segment
    dw 0xffff
    dw 0
    db 0
    db 0x92
    db 0
    db 0
gdt_end:

section .bss
align 16
stack_bottom:
    resb 16384
stack_top: