#include <stddef.h>
#include <stdatomic.h>
#include <kernel/arch/x86_64/tss.h>
#include <kernel/arch/x86_64/smp.h>
#include <kernel/arch/x86_64/hpet.h>
#include <kernel/arch/x86_64/user.h>
#include <kernel/arch/x86_64/lapic.h>
#include <kernel/fd.h>
#include <kernel/vfs.h>
#include <kernel/mmu.h>
#include <kernel/vma.h>
#include <kernel/list.h>
#include <kernel/acpi.h>
#include <kernel/sched.h>
#include <kernel/panic.h>
#include <kernel/malloc.h>
#include <kernel/signal.h>
#include <kernel/assert.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/spinlock.h>

static long next_pid = 1, next_cpu = 0;

static void sigchld(struct process *proc, int exit) {
    proc->child_exit = exit;
    sched_unblock(proc);
}

static void sigint(struct process *proc, int _) {
    sched_kill(proc, 128 + SIGINT);
}

void send_signal(struct process *proc, int signal, int extra) {
    if (!proc || signal < 1 || signal > 32) {
        return;
    }
    
    sched_lock();
    
    proc->pending_signals |= (1 << (signal - 1));
    
    if (signal == SIGCHLD) {
        proc->child_exit = extra;
    }
    proc->state = TASK_SIGNAL;
    
    sched_unlock();
}

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

void sched_add_task(struct process *proc, struct cpu *core) {
    sched_lock();

    proc->pid = next_pid++;
    if (!core) {
        core = get_core(next_cpu);
    }
    
    list_insert(core->processes, proc);

    next_cpu++;
    if (next_cpu >= madt_lapics)
        next_cpu = 0;
    else if (next_cpu < 0)
        next_cpu = madt_lapics - 1;
    sched_unlock();
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
    // FIXME kernel tasks don't really use this...
    proc->fd_table[0] = fd_new(vfs_open(vfs_root, "/dev/keyboard", false, false), 0);
    proc->fd_table[1] = fd_new(vfs_open(vfs_root, "/dev/console", false, false), 0);
    proc->fd_table[2] = fd_new(vfs_open(vfs_root, "/dev/console", false, false), 0);
    proc->vma = NULL;

    return proc;
}

struct process *sched_new_user_task(void *entry, const char *name, int argc, char *argv[], char *env[]) {
    if (!argc || !argv) {
        argc = 1;
        argv[0] = (char *)name;
        argv[1] = NULL;
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

    memset(VIRTUAL_IDENT(stack_bottom_phys), 0, (USER_STACK_SIZE * PAGE_SIZE));
    long depth = 16;

    int envc = 0;
    if (env) for (; env[envc]; envc++);

    if ((argc + envc) % 2 == 0) {
        depth += 8;
    }

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

    // TODO: clean this up with macros

    depth += 8;
    *VIRTUAL_IDENT(stack_top_phys - depth) = 0;

    for (i = envc - 1; i >= 0; i--) {
        depth += 8;
        *VIRTUAL_IDENT(stack_top_phys - depth) = env_ptrs[i];
    }

    depth += 8;
    *VIRTUAL_IDENT(stack_top_phys - depth) = 0;

    for (i = argc - 1; i >= 0; i--) {
        depth += 8;
        *VIRTUAL_IDENT(stack_top_phys - depth) = argv_ptrs[i];
    }

    depth += 8;
    *VIRTUAL_IDENT(stack_top_phys - depth) = argc;
    
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
    proc->kernel_stack = (uint64_t)kernel_stack + (4 * PAGE_SIZE); // FIXME alignment
    proc->kernel_stack_bottom = (uint64_t)kernel_stack;
    proc->gs = (uint64_t)proc;
    proc->fs = 0;
    proc->state = TASK_RUNNING;
    proc->user = true;
    proc->fd_table[0] = fd_new(vfs_open(vfs_root, "/dev/keyboard", false, false), 0);
    proc->fd_table[1] = fd_new(vfs_open(vfs_root, "/dev/console", false, false), 0);
    proc->fd_table[2] = fd_new(vfs_open(vfs_root, "/dev/console", false, false), 0);
    proc->vma = vma_create();
    proc->signal_handlers[SIGCHLD] = sigchld;
    proc->signal_handlers[SIGINT] = sigint;
    uint32_t *mxcsr = (uint32_t *)(proc->fxsave + 24);
    *mxcsr = 0x1920;
    *mxcsr |= 0x8040;
    proc->children = NULL;
    proc->parent = NULL;
    proc->cwd = vfs_root;

    return proc;
}

void sched_schedule(struct registers *r) {
    sched_lock();

    struct process *current_proc = this;
    
    if (current_proc && current_proc->state == TASK_KILLED) {
        this_core()->current_proc = NULL;
        current_proc = NULL;
    }
    
    if (current_proc) {
        if (current_proc->state != TASK_FRESH) {
            memcpy(&(current_proc->ctx), r, sizeof(struct registers));
            current_proc->gs = read_kernel_gs();
            current_proc->user_gs = read_gs();
            asm volatile ("fxsave %0 " : : "m"(current_proc->fxsave));
        } else {
            current_proc->state = TASK_RUNNING;
        }
    } else if (this_core()->processes->head) {
        this_core()->current_proc = this_core()->processes->head;
        current_proc = (struct process *)this_core()->current_proc->value;
    }

    size_t hpet_ticks = hpet_get_ticks();
    if (current_proc && current_proc->state == TASK_RUNNING)
        current_proc->time.last = hpet_ticks - current_proc->time.start;

    struct node *start = this_core()->current_proc;
    struct node *current = start;
    struct process *next = NULL;
    
    do {
        if (current && current->next) {
            current = current->next;
        } else {
            current = this_core()->processes->head;
        }
        if (!current) break;
        
        struct process *proc = (struct process *)current->value;
        
        if (proc->state == TASK_KILLED) {
            continue;
        }
        
        if (proc->state == TASK_SLEEPING &&
            hpet_ticks >= proc->time.end) {
            proc->state = TASK_RUNNING;
            proc->time.last = proc->time.end - proc->time.start;
        }
        
        if (proc->state == TASK_SIGNAL) {
            uint32_t pending = proc->pending_signals;
            proc->pending_signals = 0;
            proc->state = TASK_RUNNING;
            
            for (int sig = 1; sig <= 32; sig++) {
                uint32_t sig_mask = 1 << (sig - 1);
                
                if ((pending & sig_mask) && proc->signal_handlers[sig]) {
                    int extra = (sig == SIGCHLD) ? proc->child_exit : 0;
                    proc->signal_handlers[sig](proc, extra);
                }
            }
        }

        if (proc->state == TASK_RUNNING) {
            next = proc;
            this_core()->current_proc = current;
            goto schedule;
        }
    } while (current != start);

    next = this_core()->idle_proc;
schedule:
    next->time.start = hpet_ticks;

    memcpy(r, &(next->ctx), sizeof(struct registers));
    if (this_core()->pml4 != next->pml4) {
        vmm_switch_pm(next->pml4);
    }
    write_kernel_gs((uint64_t)next);
    write_gs(next->user_gs);
    set_kernel_stack(next->kernel_stack);
    asm volatile ("fxrstor %0 " : : "m"(next->fxsave));
    wrmsr(IA32_FS_BASE, next->fs);

    sched_unlock();
}

void sched_yield(void) {
    //lapic_ipi(this_core()->lapic_id, 0x79);
    asm volatile ("int $0x79\n");
}

void sched_block(enum process_state reason) {
    struct process *current_proc = this;
    if (current_proc) {
        current_proc->state = reason;
    }
    sched_yield();
}

void sched_unblock(struct process *proc) {
    proc->state = TASK_RUNNING;
}

void sched_sleep(int us) {
    if (us == 0) return;
    struct process *current_proc = this;
    if (current_proc) {
        current_proc->time.end = hpet_get_ticks() + (us * 1000000000ULL) / hpet_period;
    }
    sched_block(TASK_SLEEPING);
}

void sched_kill(struct process *proc, int status) {
    if (!proc) return;
    
    sched_lock();

    if (proc->pid == 1) {
        panic("Attempted to kill init!");
    }
    if (proc->pid == 0) {
        panic("Attempted to kill idle!");
    }

    if (proc->state == TASK_KILLED) {
        sched_unlock();
        return;
    }

    if (proc->parent && proc->parent->state != TASK_RUNNING) {
        send_signal(proc->parent, SIGCHLD, status);
    }
    
    proc->state = TASK_KILLED;
    
    struct cpu *proc_core = NULL;
    for (uint32_t i = 0; i < madt_lapics; i++) {
        struct cpu *core = get_core(i);
        if (list_find(core->processes, proc)) {
            proc_core = core;
            break;
        }
    }
    
    if (proc_core) {
        list_remove_value(proc_core->processes, proc);
        list_insert(proc_core->terminated_processes, proc);
        
        if (proc_core->current_proc && proc_core->current_proc->value == proc && proc != this) {
            proc_core->current_proc = NULL;
        }
        sched_unblock(proc_core->cleaner_proc);
    }
    
    sched_unlock();

    if (proc == this) {
        sched_yield();
        __builtin_unreachable();
    }
}

void sched_cleaner(void) {
    for (;;) {
        sched_lock();
        
        struct process *proc = (struct process *)list_pop(this_core()->terminated_processes);
        if (!proc) {
            sched_unlock();
            sched_block(TASK_PAUSED);
            continue;
        }
        
        if (proc->user) {
            //printf("Killing %d - %s!\n", proc->pid, proc->name);
            if (proc->parent) {
                proc->parent->children = NULL;
            }
            
            extern atomic_flag flanterm_lock;
            release(&flanterm_lock);

            this_core()->pml4 = proc->pml4;
            if (proc->sections[0].length > 0) {
                for (int i = 0; proc->sections[i].length; i++) {
                    //mmu_free((void *)mmu_get_physical(proc->pml4, proc->sections[i].ptr), ALIGN_UP(proc->sections[i].length, PAGE_SIZE) / PAGE_SIZE);
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
        
        kfree(proc);
        sched_unlock();
    }
}

void sched_idle(void) {
    for (;;) {
        asm volatile ("hlt");
    }
}

struct process *sched_find_process_by_pid(long pid) {
    for (uint32_t i = 0; i < madt_lapics; i++) {
        struct cpu *core = get_core(i);
        
        foreach(node, core->processes) {
            struct process *proc = (struct process *)node->value;
            if (proc && proc->pid == pid) {
                return proc;
            }
        }
    }
    return NULL;
}

void sched_start_all_cores(void) {
    for (uint32_t i = 0; i < madt_lapics; i++) {
        struct cpu *core = get_core(i);
        
        struct process *cleaner = sched_new_task(sched_cleaner, "System");
        cleaner->pid = next_pid;
        cleaner->state = TASK_PAUSED;
        core->cleaner_proc = cleaner;
        sched_add_task(cleaner, core);
        next_pid--;
    }
    next_pid++;

    irq_register(0x79 - 32, sched_schedule);
    for (uint32_t i = madt_lapics - 1; i >= 0; i--) {
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
        core->idle_proc = idle;
        sched_add_task(idle, core);
        idle->pid = 0;
    }
    next_pid = 1;
    dprintf("%s:%d: created %u idle processes\n", __FILE__, __LINE__, madt_lapics);
}