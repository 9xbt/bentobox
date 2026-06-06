#include <stdbool.h>
#include <kernel/module.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/malloc.h>
#include <kernel/assert.h>
#include <kernel/elf64.h>
#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/list.h>
#include <kernel/file.h>
#include <kernel/ksym.h>
#include <kernel/mmu.h>
#include <kernel/vfs.h>
#include <limine.h>
#include <stddef.h>

list_t *elf64_modules = NULL;

static Elf64_Addr elf64_find_symbol(Elf64_Sym *symtab, const char *strtab, int symbol_count, const char *str) {
    for (int i = 0; i < symbol_count; i++) {
        if (!strcmp(&strtab[symtab[i].st_name], str)) {
            return symtab[i].st_value;
        }
    }
    return 0;
}

/*
 * Credits: https://github.com/klange/toaruos/blob/master/kernel/misc/elf64.c
 */
static uint32_t aarch64_imm_adr(uint32_t val) {
	uint32_t low  = (val & 0x3) << 29;
	uint32_t high = ((val >> 2) & 0x7ffff) << 5;
	return low | high;
}

static uint32_t aarch64_imm_12(uint32_t val) {
	return (val & 0xFFF) << 10;
}

static bool elf64_is_executable(Elf64_Ehdr *ehdr) {
    if (memcmp(ehdr->e_ident, "\x7f""ELF", 4)) {
        dprintf(LOG_DEBUG, "\033[93melf:\033[0m invalid elf file\n");
        return false;
    }

    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        dprintf(LOG_DEBUG, "\033[93melf:\033[0m unsupported elf class\n");
        return false;
    }

    if (ehdr->e_machine != EM_NONE &&
    #ifdef __x86_64__
        ehdr->e_machine != EM_X86_64) {
    #elif __aarch64__
        ehdr->e_machine != EM_AARCH64) {
    #endif
        dprintf(LOG_DEBUG, "\033[93melf:\033[0m wrong architecture 0x%x\n", ehdr->e_machine);
        return false;
    }

    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
        dprintf(LOG_DEBUG, "\033[93melf:\033[0m unsupported elf type\n");
        return false;
    }

    return true;
}

__attribute__((no_sanitize("alignment")))
int elf64_module(struct limine_file *mod) {
    if (!elf64_modules)
        elf64_modules = list_create();

    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)(uintptr_t)mod->address;

    if (memcmp(ehdr->e_ident, "\x7f""ELF", 4)) {
        dprintf(LOG_ERR, "\033[93melf:\033[0m invalid elf file\n");
        return -ENOEXEC;
    }

    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        dprintf(LOG_ERR, "\033[93melf:\033[0m unsupported elf class\n");
        return -ENOEXEC;
    }

    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_REL) {
        dprintf(LOG_ERR, "\033[93melf:\033[0m unsupported elf type\n");
        return -ENOEXEC;
    }

    Elf64_Shdr *shdr = (Elf64_Shdr *)(mod->address + ehdr->e_shoff);
    Elf64_Sym *symtab = NULL;
    char *strtab = NULL;
    size_t symbol_count = 0, real_symbol_count = 0;

    if (ehdr->e_type == ET_EXEC) {
        for (int i = 0; i < ehdr->e_shnum; i++) {
            if (shdr[i].sh_type == SHT_SYMTAB) {
                symtab = (Elf64_Sym *)(mod->address + shdr[i].sh_offset);
                symbol_count = shdr[i].sh_size / shdr[i].sh_entsize;
                strtab = (char *)(mod->address + shdr[shdr[i].sh_link].sh_offset);

                ksym_expand(symbol_count);
                for (size_t j = 0; j < symbol_count; j++) {
                    real_symbol_count += ksym_register(&strtab[symtab[j].st_name], symtab[j].st_value);
                }
                dprintf(LOG_INFO, "\033[93melf:\033[0m registered %ld kernel symbols\n", real_symbol_count);
                return 0;
            }
        }
        return -EINVAL;
    }

    uintptr_t base = (uintptr_t)mmu_map_module((uintptr_t)mod->address, mod->size);
    dprintf(LOG_DEBUG, "\033[93melf:\033[0m loading %s at 0x%p\n", mod->path, base);

    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (shdr[i].sh_type == SHT_NOBITS && shdr[i].sh_size > 0) {
            shdr[i].sh_addr = (uintptr_t)mmu_map_module_bss(ALIGN_UP(shdr[i].sh_size, PAGE_SIZE) / PAGE_SIZE);
        } else if (shdr[i].sh_size > 0) {
            shdr[i].sh_addr = (uintptr_t)(base + shdr[i].sh_offset);
        }
        
        if (shdr[i].sh_type == SHT_SYMTAB) {
            symtab = (Elf64_Sym *)(mod->address + shdr[i].sh_offset);
            symbol_count = shdr[i].sh_size / sizeof(Elf64_Sym);
            strtab = (char *)(mod->address + shdr[shdr[i].sh_link].sh_offset);
        }
    }

    for (uint64_t sym = 0; sym < symbol_count; sym++) {
        if (symtab[sym].st_shndx > 0 && symtab[sym].st_shndx < SHN_LOPROC) {
            symtab[sym].st_value += shdr[symtab[sym].st_shndx].sh_addr;
        } else if (symtab[sym].st_shndx == SHN_UNDEF && symtab[sym].st_name) {
            if (!(symtab[sym].st_value = ksym_addr(&strtab[symtab[sym].st_name]))) {
                dprintf(LOG_ERR, "\033[93melf:\033[0m failed to resolve symbol: %s\n", &strtab[symtab[sym].st_name]);
            }
        }
    }

    ksym_expand(symbol_count);
    for (size_t j = 0; j < symbol_count; j++) {
        if (symtab[j].st_value == 0 || symtab[j].st_name == 0)
            continue;
        real_symbol_count += ksym_register(&strtab[symtab[j].st_name], symtab[j].st_value);
    }

    struct Module *metadata = (struct Module *)(elf64_find_symbol(symtab, strtab, symbol_count, "metadata"));
    if (!metadata) {
        dprintf(LOG_ERR, "\033[93melf:\033[0m module metadata not found for \"%s\"\n", mod->string);
        return -1;
    }

    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (shdr[i].sh_type != SHT_RELA) continue;
        if (shdr[i].sh_info >= ehdr->e_shnum) continue;

        Elf64_Rela *rela = (Elf64_Rela *)(mod->address + shdr[i].sh_offset);
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
                case R_AARCH64_ABS64:
                    T64 = S + A;
                    break;
                case R_AARCH64_ABS32:
                    T32 = (uint32_t)(S + A);
                    break;
                case R_AARCH64_ADR_PREL_PG_HI21:
					T32 = T32 | aarch64_imm_adr(((S + A) >> 12) - (P >> 12));
					break;
                case R_AARCH64_ADD_ABS_LO12_NC:
                    T32 = (T32 & 0xFFC003FF) | aarch64_imm_12(S + A);
                    break;
                case R_AARCH64_JUMP26:
                case R_AARCH64_CALL26:
					T32 = T32 | (((S + A - P) >> 2) & 0x3ffffff);
					break;
                case R_AARCH64_LDST64_ABS_LO12_NC:
                    T32 = T32 | aarch64_imm_12(((S + A) >> 3) & 0x1FF);
                    break;
                default:
                    dprintf(LOG_ERR, "\033[93melf:\033[0m unsupported relocation %ld\n", ELF64_R_TYPE(rela[j].r_info));
                    break;
            }

            #undef S
            #undef A
            #undef P
            #undef T32
            #undef T64
        }
    }

    list_insert(elf64_modules, metadata);
    return metadata->init();
}

void elf64_shutdown_modules(void) {
    if (!elf64_modules)
        return;

    foreach(i, elf64_modules) {
        struct Module *metadata = i->value;
        metadata->fini();
    }
}

static void elf64_load_sections(struct process *proc, Elf64_Ehdr *ehdr, Elf64_Phdr *phdr, uintptr_t base) {
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            uintptr_t p_vaddr = phdr[i].p_vaddr + base;

            #ifdef __x86_64__
            uint64_t flags = PTE_PRESENT | PTE_USER;
            if (phdr[i].p_flags & PF_W) flags |= PTE_WRITABLE;
            if (!(phdr[i].p_flags & PF_X)) flags |= PTE_NX;
            #elif __aarch64__
            uint64_t flags = PTE_VALID | PTE_AF | (phdr[i].p_flags & PF_W ? PTE_USER_RW : PTE_USER_RO);
            if (!(phdr[i].p_flags & PF_X)) flags |= PTE_UXN;
            #endif
            
            vmalloc(proc->vma, proc->pm, ALIGN_DOWN(p_vaddr, PAGE_SIZE),
                0, (ALIGN_UP(p_vaddr + phdr[i].p_memsz, PAGE_SIZE) -
                ALIGN_DOWN(p_vaddr, PAGE_SIZE)) / PAGE_SIZE, flags);

            if (phdr[i].p_filesz > 0) {
				uintptr_t src = (uintptr_t)ehdr + phdr[i].p_offset;
				uintptr_t dest = p_vaddr;
				size_t remaining = phdr[i].p_filesz;
				
				while (remaining > 0) {
					void *vaddr = (void *)ALIGN_DOWN(dest, PAGE_SIZE);
					void *paddr = (void *)mmu_get_physical(proc->pm, vaddr);
					uintptr_t offset = dest - ALIGN_DOWN(dest, PAGE_SIZE);
					size_t len = (remaining > PAGE_SIZE - offset) ? PAGE_SIZE - offset : remaining;

					memcpy(VIRTUAL_HHDM(paddr) + offset, (void *)src, len);
					src += len, dest += len, remaining -= len;
				}
			}

			if (phdr[i].p_memsz > phdr[i].p_filesz) {
				uintptr_t dest = p_vaddr + phdr[i].p_filesz;
				size_t remaining = phdr[i].p_memsz - phdr[i].p_filesz;

				while (remaining > 0) {
					void *vaddr = (void *)ALIGN_DOWN(dest, PAGE_SIZE);
					void *paddr = (void *)mmu_get_physical(proc->pm, vaddr);
                    
					uintptr_t offset = dest - ALIGN_DOWN(dest, PAGE_SIZE);
					size_t len = (remaining > PAGE_SIZE - offset) ? PAGE_SIZE - offset : remaining;

					memset(VIRTUAL_HHDM(paddr) + offset, 0, len);
					dest += len, remaining -= len;
				}
			}
        }
    }
}

static uintptr_t elf64_load_interp(struct process *proc, char *interp) {
    vfs_result_t r = vfs_open(NULL, interp, 0);
    if (!r.node)
        return r.error;

    vfs_node_t *node = r.node;
    void *buffer = kmalloc(node->size);
    long len = vfs_read(node, buffer, 0, node->size);
    if (len < 0) {
        kfree(buffer);
        vfs_close(node);
        return len;
    }

    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)buffer;
    if (!elf64_is_executable(ehdr)) {
        kfree(buffer);
        vfs_close(node);
        return -ENOEXEC;
    }

    Elf64_Phdr *phdr = (Elf64_Phdr *)((uintptr_t)buffer + ehdr->e_phoff);
    elf64_load_sections(proc, ehdr, phdr, INTERP_BASE);

    Elf64_Dyn *dynamic = NULL;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_DYNAMIC) {
            dynamic = (Elf64_Dyn *)(INTERP_BASE + phdr[i].p_vaddr);
            break;
        }
    }

    if (!dynamic) {
        dprintf(LOG_DEBUG, "\033[93melf:\033[0m %s has no PT_DYNAMIC section\n", interp);
        kfree(buffer);
        vfs_close(node);
        return -ENOEXEC;
    }

    uintptr_t *pm = mmu_get_pm();
    mmu_switch_pm(proc->pm);

    Elf64_Rela *rela = NULL;
    size_t rela_sz = 0;
    size_t rela_ent = sizeof(Elf64_Rela);

    for (Elf64_Dyn *d = dynamic; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
            case DT_RELA:
                rela = (Elf64_Rela *)(INTERP_BASE + d->d_un.d_ptr);
                break;
            case DT_RELASZ:
                rela_sz = d->d_un.d_val;
                break;
            case DT_RELAENT:
                rela_ent = d->d_un.d_val;
                break;
        }
    }

    if (!rela || !rela_sz) {
        dprintf(LOG_DEBUG, "\033[93melf:\033[0m %s has no PT_DYNAMIC section\n", interp);
        mmu_switch_pm(pm);
        kfree(buffer);
        vfs_close(node);
        return -ENOEXEC;
    }

    for (size_t i = 0; i < rela_sz / rela_ent; i++) {
        uintptr_t target = INTERP_BASE + rela[i].r_offset;

        #define A (rela[i].r_addend)
        #define P (target)
        #define B (INTERP_BASE)
        #define T64 (*(uint64_t*)target)

        switch (ELF64_R_TYPE(rela[i].r_info)) {
            case R_X86_64_RELATIVE:
                T64 = B + A;
                break;
        }
    }

    mmu_switch_pm(pm);
    
    uintptr_t entry = ehdr->e_entry + INTERP_BASE;
    kfree(buffer);
    vfs_close(node);
    return entry;
}

static Elf64_auxv_t *elf64_setup_auxv(Elf64_Ehdr *ehdr, Elf64_Phdr *phdr, char *interp, uintptr_t base) {
    Elf64_auxv_t *auxv = kmalloc(AUXV_COUNT * sizeof(Elf64_auxv_t));

    if (interp) {
        uintptr_t phdr_vaddr = base + ehdr->e_phoff;
        for (int i = 0; i < ehdr->e_phnum; i++) {
            if (phdr[i].p_type == PT_LOAD && 
                ehdr->e_phoff >= phdr[i].p_offset && 
                ehdr->e_phoff < phdr[i].p_offset + phdr[i].p_filesz) {
                phdr_vaddr = base + phdr[i].p_vaddr + (ehdr->e_phoff - phdr[i].p_offset);
                break;
            }
        }

        auxv[0] = (Elf64_auxv_t){ AT_PHDR,   .a_un.a_val = phdr_vaddr };
        auxv[1] = (Elf64_auxv_t){ AT_PHENT,  .a_un.a_val = ehdr->e_phentsize };
        auxv[2] = (Elf64_auxv_t){ AT_PHNUM,  .a_un.a_val = ehdr->e_phnum };
        auxv[3] = (Elf64_auxv_t){ AT_PAGESZ, .a_un.a_val = PAGE_SIZE };
        auxv[4] = (Elf64_auxv_t){ AT_BASE,   .a_un.a_val = INTERP_BASE };
        auxv[5] = (Elf64_auxv_t){ AT_ENTRY,  .a_un.a_val = base + ehdr->e_entry };
    }
    auxv[6] = (Elf64_auxv_t){ AT_NULL, .a_un.a_val = 0 };

    return auxv;
}

int spawn(const char *file, int argc, char *argv[], char *envp[]) {
    vfs_result_t r = vfs_open(NULL, file, 0);
    if (!r.node) {
        dprintf(LOG_ERR, "\033[93melf:\033[0m %s: %s\n", file, strerror(ENOENT));
        return r.error;
    }
    vfs_node_t *node = r.node;
    if (node->type == VFS_DIRECTORY) {
        dprintf(LOG_ERR, "\033[93melf:\033[0m %s: %s\n", file, strerror(EISDIR));
        vfs_close(node);
        return -EISDIR;
    }
    if (node->type != VFS_FILE) {
        dprintf(LOG_ERR, "\033[93melf:\033[0m %s: %s\n", file, strerror(EACCES));
        vfs_close(node);
        return -EACCES;
    }

    void *buffer = kmalloc(node->size);
    long len = vfs_read(node, buffer, 0, node->size);
    if (len < 0) {
        dprintf(LOG_ERR, "\033[93melf:\033[0m %s: %s\n", file, strerror(len));
        kfree(buffer);
        vfs_close(node);
        return len;
    }

    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)buffer;

    if (!elf64_is_executable(ehdr)) {
        kfree(buffer);
        vfs_close(node);
        return -ENOEXEC;
    }

    struct process *proc = sched_new_process(file, true);
    
    Elf64_Phdr *phdr = (Elf64_Phdr *)((uintptr_t)buffer + ehdr->e_phoff);
    uintptr_t base = ehdr->e_type == ET_DYN ? 0x400000 : 0;
    elf64_load_sections(proc, ehdr, phdr, base);

    char *interp = NULL;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_INTERP) {
            interp = (char *)buffer + phdr[i].p_offset;
            break;
        }
    }
    
    uintptr_t entry = interp ? elf64_load_interp(proc, interp) : ehdr->e_entry + base;
    if ((long)entry < 0) {
        dprintf(LOG_ERR, "\033[93melf:\033[0m %s: %s\n", interp, strerror(entry));
        kfree(buffer);
        vfs_close(node);
        proc->state = PROCESS_ZOMBIE;
        return (int)entry;
    }
    Elf64_auxv_t *auxv = elf64_setup_auxv(ehdr, phdr, interp, base);

    sched_new_thread(proc, (void *)entry, argc, argv, envp, auxv, AUXV_COUNT, NULL);
    sched_add_process(proc);

    kfree(auxv);
    kfree(buffer);
    vfs_close(node);
    return 0;
}

int _exec(void *buffer, int argc, char *argv[], char *envp[]) {
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)buffer;
    
    if (!elf64_is_executable(ehdr))
        return -ENOEXEC;

    acquire(&this_proc->threads->lock);
    foreach(i, this_proc->threads) {
        struct thread *tcb = i->value;
        if (tcb == this)
            continue;

        while (!sched_exit(tcb)) {
            release(&this_proc->threads->lock);
            sched_yield();
            acquire(&this_proc->threads->lock);
        }
    }
    release(&this_proc->threads->lock);
    
    while (this_proc->threads->length > 1) {
        sched_yield();
    }

    for (int fd = 0; fd < this_proc->max_files; fd++) {
        struct file *f = &this_proc->files[fd];
        if (f->open && (f->flags & O_CLOEXEC))
            file_close(fd);
    }

    kfree(this_proc->name);
    this_proc->name = strdup(argv[0]);
    
    vma_destroy(this_proc->vma, this_proc->pm);
    this_proc->vma = vma_create(SCHED_VMA_BASE, SCHED_VMA_SIZE);

    Elf64_Phdr *phdr = (Elf64_Phdr *)((uintptr_t)buffer + ehdr->e_phoff);
    uintptr_t base = ehdr->e_type == ET_DYN ? 0x400000 : 0;
    elf64_load_sections(this_proc, ehdr, phdr, base);

    char *interp = NULL;
    for (int j = 0; j < ehdr->e_phnum; j++) {
        if (phdr[j].p_type == PT_INTERP) {
            interp = (char *)buffer + phdr[j].p_offset;
            break;
        }
    }
    
    uintptr_t entry = interp ? elf64_load_interp(this_proc, interp) : ehdr->e_entry + base;
    if ((long)entry < 0) {
        dprintf(LOG_ERR, "\033[93melf:\033[0m %s: %s\n", interp, strerror(entry));
        return (int)entry;
    }
    Elf64_auxv_t *auxv = elf64_setup_auxv(ehdr, phdr, interp, base);

    struct thread *tcb = sched_new_thread(this_proc, (void *)entry, argc, argv, envp, auxv, AUXV_COUNT, NULL);
    struct cpu *cpu = sched_find_cpu();
    tcb->cpu = cpu;
    cli();
    acquire(&cpu->threads->lock);
    list_insert(cpu->threads, tcb);
    release(&cpu->threads->lock);
    sti();

    // dprintf(LOG_DEBUG, "\033[93msched:\033[0m renamed pid %d to '%s'\n", this_proc->pid, this_proc->name);
    kfree(auxv);
    return 0;
}

int exec(const char *file, int argc, char *argv[], char *envp[]) {
    vfs_result_t r = vfs_open(this_proc->cwd, file, 0);
    vfs_node_t *node = r.node;
    if (!node)
        return r.error;
    if (node->type == VFS_DIRECTORY) {
        vfs_close(node);
        return -EISDIR;
    }
    if (node->type != VFS_FILE) {
        vfs_close(node);
        return -EACCES;
    }

    void *buffer = kmalloc(node->size);
    long len = vfs_read(node, buffer, 0, node->size);
    if (len < 0) {
        kfree(buffer);
        vfs_close(node);
        return len;
    }

    int envc = 0, _argc = 0;
    if (envp) for (; envp[envc]; envc++);

    char **_argv = NULL;
    char **_envp = NULL;
    
restart_exec:
    if (len > 2 && !memcmp(buffer, "#!", 2)) {
        long sb_len = memchr(buffer + 2, '\n', node->size) - (buffer + 2);
        if (sb_len > MAX_PATH) {
            kfree(buffer);
            vfs_close(node);
            return -ENOEXEC;
        }
        char sb[sb_len + 1];
        sb[sb_len] = 0;
        memcpy(sb, buffer + 2, sb_len);

        kfree(buffer);
        vfs_close(node);

        bool has_argument = strchr(sb, ' ');
        _argc = has_argument ? argc + 2 : argc + 1;
        long path_len = has_argument ? strchr(sb, ' ') - sb : sb_len;
        char path[path_len + 1];
        memcpy(path, sb, path_len);
        path[path_len] = 0;

        r = vfs_open(this_proc->cwd, path, 0);
        node = r.node;
        if (!node)
            return r.error;
        if (node->type == VFS_DIRECTORY) {
            vfs_close(node);
            return -EISDIR;
        }
        if (node->type != VFS_FILE) {
            vfs_close(node);
            return -EACCES;
        }

        buffer = kmalloc(node->size);
        len = vfs_read(node, buffer, 0, node->size);
        if (len < 0) {
            kfree(buffer);
            vfs_close(node);
            return len;
        }

        if (len > 2 && !memcmp(buffer, "#!", 2))
            goto restart_exec;

        _argv = kmalloc((_argc + 1) * sizeof(char *));
        _envp = kmalloc((envc + 1) * sizeof(char *));
        _argv[_argc] = NULL;
        _envp[envc] = NULL;

        _argv[0] = strdup(path);
        if (has_argument)
            _argv[1] = strdup(strchr(sb, ' ') + 1);

        int i, offset = has_argument ? 2 : 1;
        for (i = 0; i < argc; i++)
            _argv[i + offset] = strdup(argv[i]);
        for (i = 0; i < envc; i++)
            _envp[i] = strdup(envp[i]);
    } else {
        _argc = argc;
        _argv = kmalloc((argc + 1) * sizeof(char *));
        _envp = kmalloc((envc + 1) * sizeof(char *));
        _argv[_argc] = NULL;
        _envp[envc] = NULL;

        int i;
        for (i = 0; i < _argc; i++)
            _argv[i] = strdup(argv[i]);
        for (i = 0; i < envc; i++)
            _envp[i] = strdup(envp[i]);
    }

    int i, ret = _exec(buffer, _argc, _argv, _envp);
    for (i = 0; i < _argc; i++)
        kfree(_argv[i]);
    for (i = 0; i < envc; i++)
        kfree(_envp[i]);

    kfree(_argv);
    kfree(_envp);
    kfree(buffer);
    vfs_close(node);
    if (!ret)
        sched_exit(this);
    return ret;
}