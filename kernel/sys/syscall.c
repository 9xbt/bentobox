#include <kernel/context.h>
#include <kernel/printf.h>

void syscall_handler(struct registers *r) {
    dprintf(LOG_INFO, "syscall %lu\n", r->rax);
    r->rax = 0;
}