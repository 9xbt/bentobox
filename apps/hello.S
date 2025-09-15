.text

.globl _start
_start:
    mov x0, #1;       // write(
    ldr x1, =msg      //   STDOUT_FILENO,
    ldr x2, =msglen   //   "Hello, world!\n",
    mov x8, #64       //   sizeof("Hello, world!\n")
    svc #0            // );

    mov x0, #0        // exit(
    mov x8, #93       //   EXIT_SUCCESS
    svc #0            // );

.data
    msg: .ascii "Hello, world!\n"
    msglen = . - msg