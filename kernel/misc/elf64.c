#include <stdbool.h>
#include <errno.h>
#include <kernel/multiboot.h>
#include <kernel/unixpipe.h>
#include <kernel/module.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/elf64.h>
#include <kernel/sched.h>
#include <kernel/list.h>
#include <kernel/mmu.h>

Elf64_Sym *ksymtab = NULL;
char *kstrtab = NULL;
int ksym_count = 0;

Elf64_Addr elf_symbol_addr(Elf64_Sym *symtab, const char *strtab, int symbol_count, char *str, bool cast) {
    Elf64_Addr offset = 0;

    char *off = strchr(str, '+');
    if (off) {
        *off = '\0';
        offset = atoi(off + 1);
    }

    for (int i = 0; i < symbol_count; i++) {
        if (!strcmp(&strtab[symtab[i].st_name], str)) {
            if (cast) return *(Elf64_Addr *)(symtab[i].st_value) + offset;
            else return symtab[i].st_value + offset;
        }
    }
    return 0;
}

int elf_symbol_name(char *s, Elf64_Sym *symtab, const char *strtab, int symbol_count, Elf64_Addr addr) {
    if (addr >= 0x400000 && addr <= PHYS_MAP_BASE) {
        strcpy(s, "(userspace)");
        return 1;
    }
    extern void *end;
    if (addr >= (uintptr_t)&end) {
        strcpy(s, "(none)");
        return 1;
    }

    Elf64_Sym *sym = NULL;
    Elf64_Addr best = (Elf64_Addr)-1;
    
    for (int i = 0; i < symbol_count; i++) {
        if (symtab[i].st_value == 0) {
            continue;
        }
        if (symtab[i].st_value <= addr) {
            if (symtab[i].st_size > 0 && addr < symtab[i].st_value + symtab[i].st_size) {
                sym = &symtab[i];
            }
            
            Elf64_Addr distance = addr - symtab[i].st_value;
            if (distance < best) {
                best = distance;
                sym = &symtab[i];
            }
        }
    }

    if (!sym) {
        strcpy(s, "(none)");
        return 1;
    }

    sprintf(s, "%s+%lu", &strtab[sym->st_name], addr - sym->st_value);
    return 0;
}

int elf_module(struct multiboot_tag_module *mod) {
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)(uintptr_t)mod->mod_start;

    if (memcmp(ehdr->e_ident, "\x7f""ELF", 4)) {
        dprintf("%s:%d: invalid elf file\n", __FILE__, __LINE__);
        return -ENOEXEC;
    }

    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        dprintf("%s:%d: unsupported elf class\n", __FILE__, __LINE__);
        return -ENOEXEC;
    }

    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_REL) {
        dprintf("%s:%d: unsupported elf type\n", __FILE__, __LINE__);
        return -ENOEXEC;
    }

    Elf64_Shdr *shdr = (Elf64_Shdr *)(mod->mod_start + ehdr->e_shoff);
    Elf64_Sym *symtab = NULL;
    char *strtab = NULL;
    uint64_t symbol_count = 0;

    if (!strcmp(mod->string, "ksym")) {
        for (int i = 0; i < ehdr->e_shnum; i++) {
            if (shdr[i].sh_type == SHT_SYMTAB) {
                ksymtab = (Elf64_Sym *)(mod->mod_start + shdr[i].sh_offset);
                ksym_count = shdr[i].sh_size / shdr[i].sh_entsize;
                kstrtab = (char *)(mod->mod_start + shdr[shdr[i].sh_link].sh_offset);
                dprintf("%s:%d: read %ld symbols from module '%s'\n", __FILE__, __LINE__, ksym_count, mod->string);
                break;
            }
        }
        return 0;
    }

    if (ehdr->e_type != ET_REL) {
        return -ENOEXEC;
    }

    uintptr_t base = (uintptr_t)mmu_map_module((uintptr_t)mod->mod_start, mod->mod_end - mod->mod_start);

    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (shdr[i].sh_type == SHT_NOBITS && shdr[i].sh_size > 0) {
            size_t pages = ALIGN_UP(shdr[i].sh_size, PAGE_SIZE) / PAGE_SIZE;
            
            shdr[i].sh_addr = (uintptr_t)mmu_alloc(pages);
            mmu_map_pages(pages, VIRTUAL(shdr[i].sh_addr), (void *)shdr[i].sh_addr, PTE_PRESENT | PTE_WRITABLE);
            memset((void *)shdr[i].sh_addr, 0, shdr[i].sh_size);
        } else if (shdr[i].sh_size > 0) {
            shdr[i].sh_addr = (uintptr_t)(base + shdr[i].sh_offset);
        }
        
        if (shdr[i].sh_type == SHT_SYMTAB) {
            symtab = (Elf64_Sym *)shdr[i].sh_addr;
            symbol_count = shdr[i].sh_size / sizeof(Elf64_Sym);
            strtab = (char *)shdr[shdr[i].sh_link].sh_addr;
        }
    }

    for (uint64_t sym = 0; sym < symbol_count; sym++) {
        if (symtab[sym].st_shndx > 0 && symtab[sym].st_shndx < SHN_LOPROC) {
            symtab[sym].st_value += shdr[symtab[sym].st_shndx].sh_addr;
        } else if (symtab[sym].st_shndx == SHN_UNDEF && symtab[sym].st_name) {
            if (!(symtab[sym].st_value = elf_symbol_addr(ksymtab, kstrtab, ksym_count, &strtab[symtab[sym].st_name], false))) {
                dprintf("%s:%d: failed to resolve symbol: %s\n", __FILE__, __LINE__, &strtab[symtab[sym].st_name]);
            }
        }
    }

    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (shdr[i].sh_type != SHT_RELA) continue;
        if (shdr[i].sh_info >= ehdr->e_shnum) continue;

        Elf64_Rela *rela = (Elf64_Rela *)shdr[i].sh_addr;
        int rela_count = shdr[i].sh_size / sizeof(Elf64_Rela);

        for (int j = 0; j < rela_count; j++) {
            uintptr_t target = rela[j].r_offset + shdr[shdr[i].sh_info].sh_addr;
            
            #define S (symtab[ELF64_R_SYM(rela[j].r_info)].st_value)
            #define A (rela[j].r_addend)
            #define P (target)
            #define T32 (*(uint32_t*)target)
            #define T64 (*(uint64_t*)target)
            
            switch (ELF64_R_TYPE(rela[j].r_info)) {
                case R_X86_64_64:
                    T64 = S + A;
                    break;
                case R_X86_64_32:
                    T32 = (uint32_t)(S + A);
                    break;
                case R_X86_64_PC32:
                    T32 = (uint32_t)(S + A - P);
                    break;
                default:
                    dprintf("%s:%d: unsupported relocation %ld\n", __FILE__, __LINE__, ELF64_R_SYM(rela[j].r_info));
                    break;
            }
        }
    }

    struct Module *metadata = (struct Module *)(elf_symbol_addr(symtab, strtab, symbol_count, "metadata", false));
    if (!metadata) {
        dprintf("%s:%d: Module metadata not found for \"%s\"\n", __FILE__, __LINE__, mod->string);
        return -1;
    }

    return metadata->init();
}

static void elf_load_sections(struct process *proc, Elf64_Ehdr *ehdr, Elf64_Phdr *phdr) {
    //dprintf("%s:%d: mapping sections\n", __FILE__, __LINE__);
    
    int i, section = 0;
    for (i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            uintptr_t page_start = ALIGN_DOWN(phdr[i].p_vaddr, PAGE_SIZE);
            uintptr_t page_end = ALIGN_UP(phdr[i].p_vaddr + phdr[i].p_memsz, PAGE_SIZE);
            size_t pages = (page_end - page_start) / PAGE_SIZE;
            
            uint64_t flags = PTE_PRESENT | PTE_USER;
            if (phdr[i].p_flags & PF_W) flags |= PTE_WRITABLE;

            for (size_t page = 0; page < pages; page++) {
                void *paddr = mmu_alloc(1);
                void *vaddr = (void *)(page_start + page * PAGE_SIZE);

                mmu_map(vaddr, paddr, PTE_PRESENT | PTE_WRITABLE);
            }

            proc->sections[section].ptr = page_start;
            proc->sections[section].length = pages * PAGE_SIZE;
            proc->brk = proc->sections[section].ptr + proc->sections[section].length;
            section++;

            if (phdr[i].p_filesz > 0) {
                uintptr_t src = (uintptr_t)(uintptr_t)ehdr + phdr[i].p_offset;
                uintptr_t dest = phdr[i].p_vaddr;

                memcpy((void *)dest, (void *)src, phdr[i].p_filesz);
            }

            if (phdr[i].p_memsz > phdr[i].p_filesz) {
                memset((void *)(phdr[i].p_vaddr + phdr[i].p_filesz), 0, phdr[i].p_memsz - phdr[i].p_filesz);
            }

            for (size_t page = 0; page < pages; page++) {
                void *vaddr = (void *)(page_start + page * PAGE_SIZE);
                void *paddr = (void *)mmu_get_physical(this_core()->pml4, (uintptr_t)vaddr);

                mmu_map(vaddr, paddr, flags);
            }
        }
    }
}

int spawn(const char *file, int argc, char *argv[], char *env[]) {
    struct vfs_node *fptr = vfs_open(NULL, file, false, false);
    if (!fptr || fptr->type != VFS_FILE) {
        dprintf("%s:%d: cannot open file \"%s\"\n", __FILE__, __LINE__, file);
        return -ENOENT;
    }

    void *buffer = kmalloc(fptr->size);
    vfs_read(fptr, buffer, 0, fptr->size);
    
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)buffer;

    if (memcmp(ehdr->e_ident, "\x7f""ELF", 4)) {
        dprintf("%s:%d: invalid elf file\n", __FILE__, __LINE__);
        kfree(buffer);
        return -ENOEXEC;
    }

    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        dprintf("%s:%d: unsupported elf class\n", __FILE__, __LINE__);
        kfree(buffer);
        return -ENOEXEC;
    }

    if (ehdr->e_type != ET_EXEC) {
        dprintf("%s:%d: unsupported elf type\n", __FILE__, __LINE__);
        kfree(buffer);
        return -ENOEXEC;
    }

    struct process *proc = sched_new_user_task((void *)ehdr->e_entry, file, argc, argv, env);

    sched_lock();
    vmm_switch_pm(proc->pml4);
    
    Elf64_Phdr *phdr = (Elf64_Phdr *)((uintptr_t)buffer + ehdr->e_phoff);
    elf_load_sections(proc, ehdr, phdr);

    vmm_switch_pm(kernel_pd);
    sched_unlock();
    
    kfree(buffer);
    sched_add_task(proc, NULL);
    sched_yield();
    return proc->pid;
}

int exec(const char *file, int argc, char *const argv[], char *const env[]) {
    struct vfs_node *fptr = vfs_open(this->cwd, file, false, false);
    if (!fptr || fptr->type != VFS_FILE) {
        //printf("%s:%d: cannot open file \"%s\"\n", __FILE__, __LINE__, file);
        return -ENOENT;
    }

    void *buffer = kmalloc(fptr->size);
    vfs_read(fptr, buffer, 0, fptr->size);
    
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)buffer;

    if (memcmp(ehdr->e_ident, "\x7f""ELF", 4)) {
        kfree(buffer);

        if (!strcmp(file, "/bin/bash") || !strcmp(file, "/usr/bin/bash")) return -ENOEXEC;

        int new_argc = 0;
        if (argv) for (; argv[new_argc]; new_argc++);
        new_argc++;

        char *new_argv[new_argc];
        for (int i = 2; i < new_argc + 1; i++) {
            new_argv[i] = argv[i - 1];
        }
        new_argv[0] = "/bin/bash";
        new_argv[1] = (char *)file;
        new_argv[new_argc] = 0;

        return exec(new_argv[0], new_argc, new_argv, env);
    }

    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        dprintf("%s:%d: unsupported elf class\n", __FILE__, __LINE__);
        kfree(buffer);
        return -ENOEXEC;
    }

    if (ehdr->e_type != ET_EXEC) {
        dprintf("%s:%d: unsupported elf type\n", __FILE__, __LINE__);
        kfree(buffer);
        return -ENOEXEC;
    }

    sched_lock();
    kfree(this->name);
    this->name = kmalloc(strlen(argv[0]) + 1);
    strcpy(this->name, argv[0]);
    this->ctx.rip = ehdr->e_entry;
    this->state = TASK_FRESH;

    int envc = 0;
    if (env) for (; env[envc]; envc++);

    uintptr_t stack_top_phys = this->stack_bottom_phys + (USER_STACK_SIZE * PAGE_SIZE);
    long depth = ((argc + envc) % 2 == 0) ? 24 : 16;

    uint64_t argv_ptrs[argc + 1];
    uint64_t env_ptrs[envc + 1];
    argv_ptrs[argc] = 0;
    env_ptrs[envc] = 0;

    int i = 0, len;
    for (i = 0; i < envc; i++) {
        len = strlen(env[i]) + 1;
        env_ptrs[i] = (uint64_t)(USER_STACK_TOP - (depth += ALIGN_UP(len, 16)));
        memmove((char *)VIRTUAL_IDENT(stack_top_phys - depth), env[i], len);
    }
    for (i = 0; i < argc; i++) {
        len = strlen(argv[i]) + 1;
        argv_ptrs[i] = (uint64_t)(USER_STACK_TOP - (depth += ALIGN_UP(len, 16)));
        memmove((char *)VIRTUAL_IDENT(stack_top_phys - depth), argv[i], len);
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
    
    this->ctx.rsp = USER_STACK_TOP - depth;
    memset(VIRTUAL_IDENT(this->stack_bottom_phys), 0, (USER_STACK_SIZE * PAGE_SIZE) - depth);
    
    vma_destroy(this->vma);
    this->vma = vma_create();

    if (this->sections[0].length > 0) {
        for (i = 0; this->sections[i].length; i++) {
            for (size_t j = 0; j < ALIGN_UP(this->sections[i].length, PAGE_SIZE) / PAGE_SIZE; j++) {
                mmu_free((void *)mmu_get_physical(this->pml4, this->sections[i].ptr + j * PAGE_SIZE), 1);
            }
            mmu_unmap_pages(ALIGN_UP(this->sections[i].length, PAGE_SIZE) / PAGE_SIZE, (void *)this->sections[i].ptr);
            this->sections[i].length = 0;
            this->sections[i].ptr = 0;
        }
    }

    Elf64_Phdr *phdr = (Elf64_Phdr *)((uintptr_t)buffer + ehdr->e_phoff);
    elf_load_sections(this, ehdr, phdr);

    sched_unlock();
    
    kfree(buffer);
    sched_yield();
    return -1;
}

long fork(struct registers *r) {
    sched_lock();

    struct process *proc = (struct process *)kmalloc(sizeof(struct process));
    memset(proc, 0, sizeof(struct process));
    proc->pml4 = mmu_create_user_pm(proc);
    this_core()->pml4 = proc->pml4;

    uintptr_t stack_top = USER_STACK_TOP;
    uintptr_t stack_bottom = stack_top - (USER_STACK_SIZE * PAGE_SIZE);
    uintptr_t stack_bottom_phys = (uintptr_t)mmu_alloc(USER_STACK_SIZE);
    uint64_t *kernel_stack = VIRTUAL(mmu_alloc(4));
    mmu_map_pages(USER_STACK_SIZE, (void *)stack_bottom, (void *)stack_bottom_phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    mmu_map_pages(4, kernel_stack, PHYSICAL(kernel_stack), PTE_PRESENT | PTE_WRITABLE);

    memcpy(VIRTUAL_IDENT(stack_bottom_phys), VIRTUAL_IDENT(this->stack_bottom_phys), (USER_STACK_SIZE * PAGE_SIZE));
    memcpy(kernel_stack, (void *)this->kernel_stack_bottom, (4 * PAGE_SIZE));
    
    proc->parent = this;
    proc->ctx.rdi = r->rdi;
    proc->ctx.rsi = r->rsi;
    proc->ctx.rbp = r->rbp;
    proc->ctx.rsp = this->stack;
    proc->ctx.rbx = r->rbx;
    proc->ctx.rdx = r->rdx;
    proc->ctx.rcx = r->rcx;
    proc->ctx.rax = 0;
    proc->ctx.r8 = r->r8;
    proc->ctx.r9 = r->r9;
    proc->ctx.r10 = r->r10;
    proc->ctx.r11 = r->r11;
    proc->ctx.r12 = r->r12;
    proc->ctx.r13 = r->r13;
    proc->ctx.r14 = r->r14;
    proc->ctx.r15 = r->r15;
    proc->ctx.rip = r->rcx;
    proc->ctx.cs = 0x23;
    proc->ctx.ss = 0x1b;
    proc->ctx.rflags = 0x202;
    proc->name = kmalloc(strlen(this->name) + 1);
    memcpy(proc->name, this->name, strlen(this->name) + 1);
    proc->stack = stack_top;
    proc->stack_bottom = (uint64_t)stack_bottom;
    proc->stack_bottom_phys = (uint64_t)stack_bottom_phys;
    proc->kernel_stack = (uint64_t)kernel_stack + (4 * PAGE_SIZE);
    proc->kernel_stack_bottom = (uint64_t)kernel_stack;
    proc->state = TASK_RUNNING;
    proc->user = true;
    proc->gs = this->gs;
    proc->fs = this->fs;
    proc->children = list_create();
    list_insert(this->children, proc);
    proc->parent = this;
    proc->cwd = this->cwd;
    memcpy(proc->fxsave, this->fxsave, sizeof proc->fxsave);
    memcpy(proc->fd_table, this->fd_table, sizeof proc->fd_table);
    for (int i = 0; i < USER_MAX_FDS; i++) {
        /** TODO: make this a separate function? */
        struct fd *fd = &proc->fd_table[i];
        if (fd->node->type == VFS_UNIXPIPE) {
            struct unix_pipe *pipe = fd->node->device;
            if (!strcmp(fd->node->name, "[pipe::read]"))
                pipe->read_refs++;
            else if (!strcmp(fd->node->name, "[pipe::write]"))
                pipe->write_refs++;
        }
    }
    memcpy(proc->sections, this->sections, sizeof proc->sections);
    memcpy(proc->signal_handlers, this->signal_handlers, sizeof proc->signal_handlers);

    for (size_t i = 0; i < sizeof this->sections / sizeof(struct process_section); i++) {
        if (this->sections[i].ptr == 0)
            break;
        for (size_t j = 0; j < ALIGN_UP(this->sections[i].length, PAGE_SIZE) / PAGE_SIZE; j++) {
            void *phys = mmu_alloc(1);
            void *virt = (void *)(this->sections[i].ptr + j * PAGE_SIZE);
            
            memcpy(VIRTUAL_IDENT(phys), VIRTUAL_IDENT(mmu_get_physical(this->pml4, (uintptr_t)virt)), PAGE_SIZE);
            mmu_map(virt, phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
        }
    }

    vmm_switch_pm(proc->pml4);
    proc->vma = vma_create();
    vma_copy_mappings(proc->vma, this->vma);
    vmm_switch_pm(this->pml4);

    sched_add_task(proc, NULL);
    sched_unlock();
    return proc->pid;
}