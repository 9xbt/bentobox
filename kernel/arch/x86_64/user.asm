section .text
    global syscall_entry
    extern syscall_handler

syscall_entry:
    swapgs
    mov [gs:16], rsp
    mov rsp, [gs:8]

    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ;push qword [gs:184]
    ;popf

    push dword 0x10202
    popf

    mov rdi, rsp
    call syscall_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax

    mov [gs:8], rsp
    mov rsp, [gs:16]
    swapgs
    o64 sysret