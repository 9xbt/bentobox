#include <stdatomic.h>
#include <stddef.h>
#include <kernel/arch/x86_64/serial.h>
#include <kernel/arch/x86_64/lapic.h>
#include <kernel/arch/x86_64/hpet.h>
#include <kernel/arch/x86_64/user.h>
#include <kernel/arch/x86_64/smp.h>
#include <kernel/arch/x86_64/tsc.h>
#include <kernel/arch/x86_64/tss.h>
#include <kernel/spinlock.h>
#include <kernel/assert.h>
#include <kernel/malloc.h>
#include <kernel/printf.h>
#include <kernel/signal.h>
#include <kernel/string.h>
#include <kernel/panic.h>
#include <kernel/sched.h>
#include <kernel/acpi.h>
#include <kernel/list.h>
#include <kernel/mmu.h>
#include <kernel/vfs.h>
#include <kernel/fd.h>

static long next_pid = 1, next_cpu = 0;

void sched_lock(void) {
#ifdef __x86_64__
    lapic_stop_timer();
#endif
}

void sched_unlock(void) {
#ifdef __x86_64__
    lapic_eoi();
    lapic_oneshot(0x79, 5);
#endif
}

node_t *sched_add_task(struct process *proc, struct cpu *core) {
    sched_lock();

    proc->pid = next_pid++;
    if (!core) {
        core = get_core(next_cpu);
    }
    node_t *node = list_insert(core->processes, proc);

    next_cpu++;
    if (next_cpu >= madt_lapics)
        next_cpu = 0;
    else if (next_cpu < 0)
        next_cpu = madt_lapics - 1;

    sched_unlock();
    return node;
}

struct process *sched_new_task(void *entry, const char *name) {
    struct process *proc = (struct process *)kmalloc(sizeof(struct process));
    memset(proc, 0, sizeof(struct process));
    proc->pml4 = this_core()->pml4;

    uint64_t *stack = VIRTUAL(mmu_alloc(4));
    mmu_map_pages(4, stack, PHYSICAL(stack), PTE_PRESENT | PTE_WRITABLE);
    memset(stack, 0, 4 * PAGE_SIZE);

    proc->ctx.rsp = (uint64_t)stack + (4 * PAGE_SIZE) - 8;
    proc->ctx.rip = (uint64_t)entry;
    proc->ctx.cs = 0x8;
    proc->ctx.ss = 0x10;
    proc->ctx.rflags = 0x202;
    proc->name = (char *)name;
    proc->stack = (uint64_t)stack + (4 * PAGE_SIZE);
    proc->stack_bottom = (uint64_t)stack;
    proc->gs = 0;
    proc->fs = 0;
    proc->state = TASK_RUNNING;
    proc->user = false;
    proc->fd_table[0] = fd_new(vfs_open(vfs_root, "/dev/console", false, false), 0);
    proc->fd_table[1] = fd_new(vfs_open(vfs_root, "/dev/console", false, false), 0);
    proc->fd_table[2] = fd_new(vfs_open(vfs_root, "/dev/console", false, false), 0);
    proc->vma = NULL;
    proc->children = list_create();
    proc->time.total = 0;

    return proc;
}

struct process *sched_new_user_task(void *entry, const char *name, int argc, char *argv[], char *env[]) {
    if (!argc || !argv) {
        argc = 1;
        static char *_argv[2];
        _argv[0] = (char *)name;
        _argv[1] = NULL;
        argv = _argv;
    }

    struct process *proc = (struct process *)kmalloc(sizeof(struct process));
    memset(proc, 0, sizeof(struct process));
    proc->pml4 = mmu_create_user_pm(proc);

    uintptr_t stack_top = USER_STACK_TOP;
    uintptr_t stack_bottom = stack_top - (USER_STACK_SIZE * PAGE_SIZE);
    uintptr_t stack_bottom_phys = (uintptr_t)mmu_alloc(USER_STACK_SIZE);
    uintptr_t stack_top_phys = stack_bottom_phys + (USER_STACK_SIZE * PAGE_SIZE);
    uint64_t *kernel_stack = VIRTUAL(mmu_alloc(4));
    mmu_map_pages(USER_STACK_SIZE, (void *)stack_bottom, (void *)stack_bottom_phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    mmu_map_pages(4, kernel_stack, PHYSICAL(kernel_stack), PTE_PRESENT | PTE_WRITABLE);

    int envc = 0;
    if (env) for (; env[envc]; envc++);

    memset(VIRTUAL_IDENT(stack_bottom_phys), 0, (USER_STACK_SIZE * PAGE_SIZE));
    long depth = ((argc + envc) % 2 == 0) ? 24 : 16;

    uint64_t argv_ptrs[argc + 1];
    uint64_t env_ptrs[envc + 1];
    argv_ptrs[argc] = 0;
    env_ptrs[envc] = 0;

    int i = 0;
    for (i = 0; i < envc; i++) {
        depth += ALIGN_UP(strlen(env[i]) + 1, 16);
        env_ptrs[i] = (uint64_t)(USER_STACK_TOP - depth);
        strcpy((char *)VIRTUAL_IDENT(stack_top_phys - depth), env[i]);
    }
    for (i = 0; i < argc; i++) {
        depth += ALIGN_UP(strlen(argv[i]) + 1, 16);
        argv_ptrs[i] = (uint64_t)(USER_STACK_TOP - depth);
        strcpy((char *)VIRTUAL_IDENT(stack_top_phys - depth), argv[i]);
    }

    *VIRTUAL_IDENT(stack_top_phys - (depth += 8)) = 0;
    for (i = envc - 1; i >= 0; i--) {
        depth += 8;
        *VIRTUAL_IDENT(stack_top_phys - depth) = env_ptrs[i];
    }

    *VIRTUAL_IDENT(stack_top_phys - (depth += 8)) = 0;
    for (i = argc - 1; i >= 0; i--) {
        *VIRTUAL_IDENT(stack_top_phys - (depth += 8)) = argv_ptrs[i];
    }

    *VIRTUAL_IDENT(stack_top_phys - (depth += 8)) = argc;
    
    proc->ctx.rsp = stack_top - depth;
    proc->ctx.rip = (uint64_t)entry;
    proc->ctx.cs = 0x23;
    proc->ctx.ss = 0x1b;
    proc->ctx.rflags = 0x202;
    proc->name = kmalloc(strlen(name) + 1);
    strcpy(proc->name, name);
    proc->stack = stack_top;
    proc->stack_bottom = (uint64_t)stack_bottom;
    proc->stack_bottom_phys = (uint64_t)stack_bottom_phys;
    proc->kernel_stack = (uint64_t)kernel_stack + (4 * PAGE_SIZE) - 8;
    proc->kernel_stack_bottom = (uint64_t)kernel_stack;
    proc->gs = (uint64_t)proc;
    proc->fs = 0;
    proc->state = TASK_RUNNING;
    proc->user = true;
    proc->fd_table[0] = fd_new(vfs_open(vfs_root, "/dev/tty1", false, false), 0);
    proc->fd_table[1] = fd_new(vfs_open(vfs_root, "/dev/tty1", false, false), 0);
    proc->fd_table[2] = fd_new(vfs_open(vfs_root, "/dev/tty1", false, false), 0);
    proc->vma = vma_create();
    proc->signal_handlers[SIGINT] = _sigint;
    proc->signal_handlers[SIGPIPE] = _sigpipe;
    proc->signal_handlers[SIGTERM] = _sigterm;
    proc->signal_handlers[SIGCHLD] = _sigchld;
    uint32_t *mxcsr = (uint32_t *)(proc->fxsave + 24);
    *mxcsr = 0x1920;
    *mxcsr |= 0x8040;
    proc->children = list_create();
    proc->parent = NULL;
    proc->cwd = vfs_root;
    proc->time.total = 0;

    return proc;
}

void sched_schedule(struct registers *r) {
    sched_lock();

    if (this_core()->current_proc) {
        if (this->state != TASK_FRESH) {
            memcpy(&(this->ctx), r, sizeof(struct registers));
            this->gs = read_kernel_gs();
            this->user_gs = read_gs();
            asm volatile ("fxsave %0 " : : "m"(this->fxsave));
        } else this->state = TASK_RUNNING;
    } else {
        this_core()->current_proc = this_core()->processes->head;
    }

    size_t hpet_ticks = hpet ? hpet_get_ticks() : tsc_get_ticks();
    if (this->state == TASK_RUNNING ||
        this_core()->current_proc == this_core()->idle_proc) {
        this->time.last = hpet_ticks - this->time.start;
        this->time.total += this->time.last;
    }

    if (!this_core()->current_proc->next) {
        this_core()->current_proc = this_core()->processes->head;
    } else {
        this_core()->current_proc = this_core()->current_proc->next;
    }

    node_t *start = this_core()->current_proc;
    foreach(node, this_core()->processes) {
        if (node != start) continue;
        
        node_t *current = node;
        do {
            if (!current) {
                if (!this_core()->processes->head) break;
                current = this_core()->processes->head;
            }
            if (!current->value) {
                current = current->next;
                continue;
            }
            
            struct process *proc = current->value;
            if (proc->state == TASK_SLEEPING && hpet_ticks >= proc->time.end) {
                proc->state = TASK_RUNNING;
                proc->time.last = proc->time.end - proc->time.start;
                this_core()->current_proc = current;
                goto actually_switch;
            }
            
            if (proc->state == TASK_SIGNAL) {
                uint32_t pending = proc->pending_signals;
                proc->pending_signals = 0;
                proc->state = TASK_RUNNING;
                
                for (int sig = 1; sig <= 32; sig++) {
                    uint32_t sig_mask = 1u << (sig - 1); // here
                    
                    if ((pending & sig_mask) && proc->signal_handlers[sig]) {
                        proc->signal_handlers[sig](proc);
                    }
                }
                //this_core()->current_proc = current;
                //goto actually_switch;
            }

            if (proc->state == TASK_RUNNING) {
                this_core()->current_proc = current;
                goto actually_switch;
            }

            current = current->next;
            if (!current) current = this_core()->processes->head;
        } while (current != start);
        break;
    }

    this_core()->current_proc = this_core()->idle_proc;

actually_switch:
    this->time.start = hpet_ticks;

    memcpy(r, &(this->ctx), sizeof(struct registers));
    if (this_core()->pml4 != this->pml4)
        vmm_switch_pm(this->pml4);
    write_kernel_gs((uint64_t)this);
    write_gs(this->user_gs);
    set_kernel_stack(this->kernel_stack);
    asm volatile ("fxrstor %0 " : : "m"(this->fxsave));
    wrmsr(IA32_FS_BASE, this->fs);

    sched_unlock();
}

void sched_yield(void) {
    //lapic_ipi(this_core()->lapic_id, 0x79);
    asm volatile ("int $0x79\n");
}

void sched_block(enum process_state reason) {
    this->state = reason;
    sched_yield();
}

void sched_unblock(struct process *proc) {
    proc->state = TASK_RUNNING;
}

void sched_sleep(long us) {
    if (us == 0) return;
    if (hpet) this->time.end = hpet_get_ticks() + (us * 1000000000ULL) / hpet_period;
    else this->time.end = tsc_get_ticks() + us * tsc_period;
    sched_block(TASK_SLEEPING);
}

struct process *sched_get_foreground(long pgid) {
    for (uint32_t i = 0; i < madt_lapics; i++) {
        struct cpu *core = get_core(i);

        foreach(proc, core->processes) {
            if (((struct process *)proc->value)->pgid == pgid)
                return proc->value;
        }
    }
    return NULL;
}

void sched_kill(struct process *proc, int status) {
    assert(proc);
    //if (proc->pid == 1)
    //    panic("Attempted to kill init!");
    if (proc->pid == 0)
        panic("Attempted to kill idle task!");

    if (proc->state == TASK_KILLED)
        return;
    if (proc->parent)
        signal_send(proc->parent, SIGCHLD, status);

    sched_lock();
    
    bool yield = proc == this;
    
    proc->state = TASK_KILLED;
    list_insert(terminated_process_list, proc);
    
    sched_unblock(this_core()->cleaner_proc->value);
    sched_unlock();

    if (yield) {
        sched_yield();
        __builtin_unreachable();
    }
}

struct process *sched_find_process(long pid) {
    for (uint32_t i = 0; i < madt_lapics; i++) {
        struct cpu *core = get_core(i);

        foreach(proc, core->processes) {
            if (((struct process *)proc->value)->pid == pid)
                return proc->value;
        }
    }
    return NULL;
}

void sched_cleaner(void) {
    for (;;) {
        sched_lock();
        
        node_t *node = terminated_process_list->head;
        if (!node || !node->value) {
            sched_unlock();
            sched_block(TASK_PAUSED);
            continue;
        }
        
        struct process *proc = node->value;

        if (proc->pending_signals)
            continue;

        list_remove(terminated_process_list, node);
        list_remove_value(process_list, proc);

        for (int i = 0; i < USER_MAX_FDS; i++) {
            struct fd *file = &proc->fd_table[i];
            if (file->open)
                vfs_close(file->node);
        }
        
        if (proc->user) {
            if (proc->parent) {
                list_remove_value(proc->parent->children, proc);
            }
            
            extern atomic_flag flanterm_lock;
            release(&flanterm_lock);

            this_core()->pml4 = proc->pml4;
            if (proc->sections[0].length > 0) {
                for (int i = 0; proc->sections[i].length; i++) {
                    for (size_t j = 0; j < ALIGN_UP(proc->sections[i].length, PAGE_SIZE) / PAGE_SIZE; j++) {
                        mmu_free((void *)mmu_get_physical(proc->pml4, proc->sections[i].ptr + j * PAGE_SIZE), 1);
                    }
                    mmu_unmap_pages(ALIGN_UP(proc->sections[i].length, PAGE_SIZE) / PAGE_SIZE, (void *)proc->sections[i].ptr);
                }
            }

            mmu_unmap_pages(USER_STACK_SIZE, (void *)proc->stack_bottom);
            mmu_unmap_pages(4, (void *)proc->kernel_stack_bottom);
            mmu_free((void *)proc->stack_bottom_phys, USER_STACK_SIZE);
            mmu_free(PHYSICAL(proc->kernel_stack_bottom), 4);
            kfree(proc->name);
            vma_destroy(proc->vma);
            mmu_destroy_user_pm(proc->pml4);
        } else {
            mmu_unmap_pages(4, (void *)proc->stack_bottom);
            mmu_free(PHYSICAL(proc->stack_bottom), 4);
        }
        list_free(proc->children);

        sched_unlock();
        kfree(proc);
    }
}

void sched_idle(void) {
    for (;;) {
        asm volatile ("hlt");
    }
}

void sched_timetrack(void) {
    uint64_t last_idle = 0;

    for (;;) {
        sched_sleep(1000000);
        struct process *idle = this_core()->idle_proc->value;

        uint64_t delta_idle = idle->time.total - last_idle;
        last_idle = idle->time.total;
        uint64_t idle_us = (delta_idle * (uint64_t)hpet_period) / 1000000000ULL;
        uint64_t busy_us = (1000000 > idle_us) ? (1000000 - idle_us) : 0;
        this_core()->usage = (busy_us * 100) / 1000000;

        //dprintf(LOG_INFO, "CPU usage: %lu%%\r", this_core()->usage);
    }
}

void sched_jumpstart(void) {
    loglevel = LOG_ERR;

    for (uint32_t i = 0; i < madt_lapics; i++) {
        struct cpu *core = get_core(i);
        
        struct process *cleaner = sched_new_task(sched_cleaner, "System");
        cleaner->pid = next_pid;
        cleaner->state = TASK_PAUSED;
        core->cleaner_proc = sched_add_task(cleaner, core);
        next_pid--;
    }
    next_pid++;

    irq_register(0x79 - 32, sched_schedule);
    for (uint32_t i = madt_lapics; i-- > 0;) {
        lapic_ipi(i, 0x79);
    }
}

void sched_install(void) {
    for (uint32_t i = 0; i < madt_lapics; i++) {
        struct cpu *core = get_core(i);
        core->processes = list_create();
        core->terminated_processes = list_create();

        struct process *idle = sched_new_task(sched_idle, "System Idle Process");
        idle->state = TASK_PAUSED;
        core->idle_proc = sched_add_task(idle, core);
        idle->pid = 0;
        idle->pgid = 0;

        //struct process *timetracker = sched_new_task(sched_timetrack, "System Load Calculator");
        //timetracker->pid = 2;
        //timetracker->pgid = 0;
        //sched_add_task(timetracker, core);
    }
    next_pid = 1;
    dprintf(LOG_INFO, "%s:%d: initialized process lists\n", __FILE__, __LINE__);
    //printf("\033[92m * \033[97mInitialized scheduler\033[0m\n");
}