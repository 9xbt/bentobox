/*
 * Credits:
 *  - aarch64_imm_adr, aarch64_imm_12: https://github.com/klange/toaruos/blob/master/kernel/misc/elf64.c
 */

#include <stdbool.h>
#include <kernel/module.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/malloc.h>
#include <kernel/elf64.h>
#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/list.h>
#include <kernel/file.h>
#include <kernel/ksym.h>
#include <kernel/mmu.h>
#include <kernel/vfs.h>
#include <limine.h>

static Elf64_Addr elf64_find_symbol(Elf64_Sym *symtab, const char *strtab, int symbol_count, const char *str) {
    for (int i = 0; i < symbol_count; i++) {
        if (!strcmp(&strtab[symtab[i].st_name], str)) {
            return symtab[i].st_value;
        }
    }
    return 0;
}

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

    #ifdef __x86_64__
    if (ehdr->e_machine != EM_X86_64) {
    #elif __aarch64__
    if (ehdr->e_machine != EM_AARCH64) {
    #endif
        dprintf(LOG_DEBUG, "\033[93melf:\033[0m wrong architecture\n");
        return false;
    }

    if (ehdr->e_type != ET_EXEC) {
        dprintf(LOG_DEBUG, "\033[93melf:\033[0m unsupported elf type\n");
        return false;
    }

    return true;
}

__attribute__((no_sanitize("alignment")))
int elf64_module(struct limine_file *mod) {
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
    dprintf(LOG_DEBUG, "\033[93melf:\033[0m loading module '%s' at base 0x%p\n", mod->path, base);

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
        }
    }

    return metadata->init();
}

static void elf64_load_sections(struct process *proc, Elf64_Ehdr *ehdr, Elf64_Phdr *phdr) {
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            #ifdef __x86_64__
            uint64_t flags = PTE_PRESENT | PTE_USER;
            if (phdr[i].p_flags & PF_W) flags |= PTE_WRITABLE;
            if (!(phdr[i].p_flags & PF_X)) flags |= PTE_NX;
            #elif __aarch64__
            uint64_t flags = PTE_VALID | PTE_AF | PTE_USER;
            flags |= (phdr[i].p_flags & PF_W) ? PTE_RW : PTE_RO;
            if (!(phdr[i].p_flags & PF_X)) flags |= PTE_UXN;
            #endif
            
            vmalloc(proc->vma, proc->pm, ALIGN_DOWN(phdr[i].p_vaddr, PAGE_SIZE),
                0, (ALIGN_UP(phdr[i].p_vaddr + phdr[i].p_memsz, PAGE_SIZE) -
                ALIGN_DOWN(phdr[i].p_vaddr, PAGE_SIZE)) / PAGE_SIZE, flags);

            if (phdr[i].p_filesz > 0) {
				uintptr_t src = (uintptr_t)ehdr + phdr[i].p_offset;
				uintptr_t dest = phdr[i].p_vaddr;
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
				uintptr_t dest = phdr[i].p_vaddr + phdr[i].p_filesz;
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

int spawn(const char *file, int argc, char *argv[], char *envp[]) {
    vfs_node_t *node = vfs_open(NULL, file, 0);
    if (!node) {
        dprintf(LOG_ERR, "\033[93melf:\033[0m %s: %s\n", file, strerror(ENOENT));
        return -ENOENT;
    }
    if (node->type == VFS_DIRECTORY) {
        dprintf(LOG_ERR, "\033[93melf:\033[0m %s: %s\n", file, strerror(EISDIR));
        vfs_close(node);
        return -EISDIR;
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
    sched_new_thread(proc, (void *)ehdr->e_entry, argc, argv, envp);
    
    Elf64_Phdr *phdr = (Elf64_Phdr *)((uintptr_t)buffer + ehdr->e_phoff);
    elf64_load_sections(proc, ehdr, phdr);

    kfree(buffer);
    vfs_close(node);

    sched_add_process(proc);
    return 0;
}

int exec(const char *file, int argc, char *argv[], char *envp[]) {
    vfs_node_t *node = vfs_open(this_proc->cwd, file, 0);
    if (!node) {
        dprintf(LOG_DEBUG, "\033[93melf:\033[0m %s: %s\n", file, strerror(ENOENT));
        return -ENOENT;
    }
    if (node->type == VFS_DIRECTORY) {
        dprintf(LOG_DEBUG, "\033[93melf:\033[0m %s: %s\n", file, strerror(EISDIR));
        vfs_close(node);
        return -EISDIR;
    }

    void *buffer = kmalloc(node->size);
    long len = vfs_read(node, buffer, 0, node->size);
    if (len < 0) {
        dprintf(LOG_DEBUG, "\033[93melf:\033[0m %s: %s\n", file, strerror(len));
        kfree(buffer);
        vfs_close(node);
        return len;
    }

    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)buffer;

    if (!memcmp(buffer, "#!", 2)) {
        long shebang_len = memchr(buffer, '\n', node->size) - buffer;
        char shebang[shebang_len + 1];
        shebang[shebang_len] = 0;
        memcpy(shebang, buffer, shebang_len);
        kfree(buffer);
        vfs_close(node);

        char **_argv = kmalloc((argc + 2) * sizeof(char *));
        memcpy(_argv + 1, argv, (argc + 1) * sizeof(char *));
        _argv[0] = shebang + 2;
        
        return exec(_argv[0], argc + 1, _argv, envp);
    }

    if (!elf64_is_executable(ehdr)) {
        kfree(buffer);
        vfs_close(node);
        return -ENOEXEC;
    }

    foreach(i, this_proc->threads) {
        struct thread *tcb = i->value;
        if (tcb != this) {
            tcb->state = THREAD_ZOMBIE;
            while (__atomic_load_n(&tcb->state, __ATOMIC_ACQUIRE) != THREAD_ZOMBIE_ACK) {
                #ifdef __x86_64__
                __builtin_ia32_pause();
                #endif
            }
        }
    }

    for (int fd = 0; fd < this_proc->max_files; fd++) {
        struct file *f = &this_proc->files[fd];
        if (f->open && (f->flags & O_CLOEXEC))
            file_close(fd);
    }

    int envc = 0;
    if (envp) for (; envp[envc]; envc++);

    char **_argv = kmalloc((argc + 1) * sizeof(char *));
    char **_envp = kmalloc((envc + 1) * sizeof(char *));
    _argv[argc] = NULL;
    _envp[envc] = NULL;

    int i;
    for (i = 0; i < argc; i++) {
        _argv[i] = strdup(argv[i]);
    }
    for (i = 0; i < envc; i++) {
        _envp[i] = strdup(envp[i]);
    }

    kfree(this_proc->name);
    this_proc->name = kmalloc(strlen(file + 1));
    strcpy(this_proc->name, file);
    
    vma_destroy(this_proc->vma, this_proc->pm);
    this_proc->vma = vma_create(SCHED_VMA_BASE, SCHED_VMA_SIZE);

    struct thread *tcb = sched_new_thread(this_proc, (void *)ehdr->e_entry, argc, _argv, _envp);    

    Elf64_Phdr *phdr = (Elf64_Phdr *)((uintptr_t)buffer + ehdr->e_phoff);
    elf64_load_sections(this_proc, ehdr, phdr);

    kfree(buffer);
    vfs_close(node);

    list_insert(sched_find_cpu()->threads, tcb);

    // dprintf(LOG_DEBUG, "\033[93msched:\033[0m renamed pid %d to '%s'\n", this_proc->pid, this_proc->name);
    sched_exit(this);
    return -1;
}