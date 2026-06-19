section .text
    global syscall_entry
    global user_copy_fail
    global signal_leave
    extern do_syscall

syscall_entry:
    swapgs
    mov [gs:24], rsp
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

    sti
    mov rdi, rsp
    call do_syscall
    cli

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
    mov rsp, [gs:24]
    swapgs
    o64 sysret

user_copy_fail:
    leave
    ret

align 4096
signal_leave:
    mov rax, 25
    syscall
    ud2