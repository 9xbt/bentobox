.text

.globl _start
_start:
    mov x8, #1        // write(
    mov x0, #1        //   STDOUT_FILENO,
    ldr x1, =msg      //   "Hello, world!\n",
    ldr x2, =msglen   //   sizeof("Hello, world!\n")
    svc #0            // );

    mov x8, #10       // exit(
    mov x0, #0        //   EXIT_SUCCESS
    svc #0            // );

.data
    msg: .ascii "Hello, world!\n"
    msglen = . - msg