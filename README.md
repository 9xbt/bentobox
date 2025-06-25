# bentobox
bentobox is a 64-bit SMP-enabled operating system targeting x86_64 and RISC-V

## Features on x86_64
- Multiboot 2 boot & module support
- 4-level paging
- VGA text mode and serial driver
- Framebuffer console
- PS/2 keyboard driver
- ACPI table parsing (MADT & FADT)
- LAPIC & IOAPIC support
- HPET timer support
- PCI driver
- SMP-aware scheduler
- Unix-style VFS
- Elf64 loading
- ATA and AHCI driver
- ext2 support with caching
- Userspace processes
- SSE support
- mlibc port

## Features on RISC-V
- Virtio UART driver

> [!NOTE]
> bentobox on RISC-V is a stub

## Building (x86_64)
To build, you need to install the following packages:
- build-essential
- nasm
- grub-pc
- xorriso
- mtools
- qemu-system-x86
- genext2fs
- meson
- curl
- git

After that, you need to build mlibc. Run `make mlibc-setup`.

Now you can build bash. Clone `https://github.com/9xbt/bash` to the same directory where `bentobox` is. Then, run `./util/bash-am.sh` and bash should build.

If you want a more complete environment, you need to set up busybox. Run `./util/busybox-setup.sh` and after that `./util/busybox.sh`.

Finally, you can simply run `make run-kvm -j` and the kernel will run in QEMU. Change the amount of cores as you desire in the Makefile.

> [!IMPORTANT]
> If you're on a Debian-based system, you have to also pass `SHELL=/bin/bash` to make. Otherwise, modules will not be built properly and will crash the kernel.

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
    - [X] Singly linked blocks
    - [X] Doubly linked blocks
    - [ ] Triply linked blocks
    - [X] Reading
    - [ ] Writing
    - [X] Mounting
    - [X] Caching
- [X] Framebuffer support
- [ ] PS/2 drivers
    - [X] Keyboard
        - [ ] Arrow key support
    - [ ] Mouse
- [ ] Userspace support
    - [X] TSS
    - [X] Ring 3 in the scheduler
    - [X] Syscall handler
    - [X] Port a libc
- [X] ELF loading
- [X] Symbol table
- [ ] Initial filesystem
- [X] `unimplemented` macro
- [X] Simplify the PCI driver
- [X] FIFO queues
~~- [ ] Write a more efficient heap~~
- [X] Move to dlmalloc
- [ ] Make an OS specific toolchain
- [X] 64-bit VFS
- [X] Module metadata headers
- [X] Allow use of symbols in debugcon.c
- [X] Use spinlocks in FIFO queues and mutexes in the ATA driver
- [X] %p in printf
- [X] Implement file descriptors
- [X] Elf execution from the filesystem
- [ ] Write an RTC driver
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
- [ ] Fix keyboard driver bugs on startup
- [X] Higher half modules (map kernel to higher half)
- [ ] Implement task threading
- [X] Implement a VMA
- [ ] Support NX bit
- [X] Fix VMA
- [X] Fix read()
- [ ] TSC timing
- [X] exec()
- [X] fork()
- [ ] ~~Fix ATA driver not reading/writing more than 256 sectors at a time~~
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
- ~~[ ] Move FD termios to VFS nodes~~
- [X] faccessat
- [X] Check FD bounds in syscalls that don't
- [ ] Module symbol parsing
- [ ] Proper VFS drivers
- [X] Fix fork() crashing because of brk() (musl bash 5.1)
- [X] Fix having a lot of children crashing the kernel
- [X] Null out serial_redirect on panic
- [ ] Change feature list to be less exhaustive
- [ ] Don't print `warning: couldn't get next pml` when just checking a mapping

## Screenshots
![image](https://github.com/user-attachments/assets/c9d56315-3818-4518-9a06-092b0b919a76)
