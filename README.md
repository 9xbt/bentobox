# bentobox ![GitHub Actions Workflow Status](https://img.shields.io/github/actions/workflow/status/9xbt/bentobox/ci.yml)

bentobox is a 64-bit SMP-enabled operating system targeting x86_64 with plans to support RISC-V

## Features on x86_64
- Full architecture support
    - GDT, IDT, LAPIC timer, (I/O)APIC, HPET, SMP, `syscall` instruction
- Terminal with VGA text mode and framebuffer console
    - PS/2 keyboard
    - Serial driver
- Memory management
    - Bitmap allocator
    - 4-level paging
    - dlmalloc-based heap
- ACPI table parsing
    - MADT/FADT tables
- Processes and scheduler
    - SMP-aware scheduler with signal support
- PCI device enumeration
- Files
    - Unix-style virtual filesystem
    - ATA/AHCI storage drivers
    - Filesystem (ext2)
- Userspace
    - Elf64 loader
    - Linux syscalls (about 15% are currently implemented)
    - Can run bash, busybox, doomgeneric and figlet

## Features on RISC-V
- Virtio UART driver

> [!NOTE]
> bentobox on RISC-V is a stub and does not build currently

## Building (x86_64)
To build, you need to install the following packages:
- build-essential
- nasm
- grub-pc
- xorriso
- mtools
- qemu-system-x86
- genext2fs
- curl
- musl

Now you can build the ports. Run `./util/ports.sh` to build all of them, or look in `ports/README.md` to see how to build each one separately.

Finally, you can simply run `make run-kvm -j$(nproc)` and the kernel will run in QEMU. By default the VM will use half of your host's threads, however this can be changed in the Makefile.

## TODO
- [X] `panic()` function
- [X] ANSI support in the VGA driver
- [X] Write a scheduler
- [X] Write a VFS
- [X] FADT cleanup
- [X] PCI
- [X] SMP
- [X] ATA driver
- [X] AHCI driver
- [X] ext2fs support
    - [X] Direct blocks
    - [X] Indirect blocks
    - [X] Doubly indirect blocks
    - [ ] Triply indirect blocks
    - [X] Reading
    - [X] Writing
    - [X] Mounting
    - [ ] Caching
- [X] Framebuffer support
- [X] PS/2 driver
    - [X] Keyboard
    - [X] Mouse
- [ ] Userspace support
    - [X] TSS
    - [X] Ring 3 in the scheduler
    - [X] Syscall handler
    - [X] Port a libc
- [X] ELF loading
- [X] Symbol table
- [ ] Initial filesystem (tarfs)
- [X] `unimplemented` macro
- [X] Simplify the PCI driver
- [X] FIFO queues
- [X] Port dlmalloc
- [X] Move to dlmalloc
- [ ] Make an OS specific toolchain
- [X] 64-bit VFS
- [X] Module metadata headers
- [X] Allow use of symbols in debugcon.c
- [X] Use spinlocks in FIFO queues and mutexes in the ATA driver
- [X] %p in printf
- [X] Implement file descriptors
- [X] Elf execution from the filesystem
- [X] Write an RTC driver
- [X] Fix ring 3 processes in SMP
- [X] Fix memory leaks
- [X] Refactor VMM to take pml4's and `void *` instead of `uintptr_t`
- [X] Fix real hardware triple faults
- [X] Fix HPET math
- [X] Better cmdline parsing
- [X] Better task killing
- [ ] Implement CoW
- [X] Fix mmap(2)
- [X] Fix memory issues with large elf64 executables
- [X] Restore `fs` on context switches
- [X] SSE support
- [X] Recursively unmap pagemaps in VMM
- [X] Fix keyboard driver bugs on startup
- [X] Higher half modules (map kernel to higher half)
- [ ] Implement task threading
- [X] Implement a VMA
- [X] Support NX bit
- [X] Fix VMA
- [X] Fix read()
- [X] TSC timing
- [X] exec()
- [X] fork()
- [X] Fix ATA driver not reading/writing more than 256 sectors at a time
- [X] waitpid()
    - [X] Signals
- [X] Fix signals on SMP
- [X] Move syscalls to other files
- [X] ioctl() font changing
- [X] tmpfs
- [X] char *const env[]
- [X] Fix bash crashing (unaligned stack)
- [X] newfstatat
- [X] uname
- [X] SIOCGWINSZ
- [X] Fix bash build
- [X] Fix mlibc symlinks
- [X] Userspace exposed framebuffer
- [X] TTY operations
- [X] Move vfs_node->open to fd->open
- [ ] Lazy ELF loading
- [X] faccessat
- [X] Check FD bounds in syscalls that don't
- [X] Module symbol parsing
- [X] Proper VFS drivers
- [X] Fix fork() crashing because of brk() (musl bash 5.1)
- [X] Fix having a lot of children crashing the kernel
- [X] Null out serial_redirect on panic
- [X] Change feature list to be less exhaustive
- [X] Don't print `warning: couldn't get next pml` when just checking a mapping
- [X] Generic lists
    - [X] Make the scheduler use them
    - [X] Make the VFS use them
- [X] UNIX pipes
- [X] Generic ringbuffers
    - [X] Make unixpipes use them
- [ ] Scheduler improvements
    - [X] Polling
    - [X] CPU usage calculation
    - [ ] Priorities
    - [ ] Process stealing
- [X] Write a TTY driver
- [ ] MBR partitions
- [X] GPT partitions
- [ ] Userspace signal handlers
    - [X] Block CTRL+C in init
- [X] Fix serial read()
- [X] sysinfo()
- [X] Write better dlmalloc glue code
- [X] VFS mounting
- [X] Fix fork() process adding
- [ ] MSI/MSI-X
- [ ] Fix gcc builds crashing

## Screenshots
![image](https://github.com/user-attachments/assets/847c395a-446b-4aea-8dad-370437ebe4fc)

*bentobox running in VGA text mode*

![image](https://github.com/user-attachments/assets/112d5412-86e5-4537-a5a9-d45f0736a95a)

*bentobox running doomgeneric*

![image](https://github.com/user-attachments/assets/a9494621-7f2e-4783-9019-e764f10a6e77)

*bentobox running an Xorg-esque GUI demo*
