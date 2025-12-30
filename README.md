# bentobox
bentobox is a 64-bit SMP-enabled operating system targeting x86_64 and aarch64.

## Features
- SMP capable preemptive kernel with full multi-architecture support
- POSIX compatibility
- IPC: Pipes, UNIX domain sockets, signals
- Ports: bash, coreutils, vim, fastfetch & more
- Filesystems: ext2, devfs, tmpfs, procfs
- Interrupt controllers: APIC (x86_64), GICv2 (aarch64)
- ACPI table parsing & full ACPI mode using LAI
- PCI scanning
- Input devices: PS/2 (x86_64), virtio-input (aarch64)
- Elf64 modules & binaries, VMM with CoW support

## Building the toolchain
### x86_64
Packages required:
- git
- base-devel

Start by building binutils. Run `./util/binutils.sh`.

Then, install mlibc headers for the GCC cross compiler. Run `make -f build/mlibc.mk headers`.

Now you can build the GCC cross compiler. Run `./util/gcc.sh`.

## Building the userspace
### x86_64
Packages required:
- git
- meson

Start by building mlibc. Run `make -f build/mlibc.mk setup build install`.

Now you can build the ports. Run `. build/mlibc-root` to source the environment.

> [!TIP]
> Run `./build/strip-bin` after building the ports to reduce their size.

Finally, run `make hdd -j$(nproc)` to make the HDD image (or `make livecd -j$(nproc)` if you prefer an initrd).

## Ports

### bash
Run `./ports/bash.sh`.

### gnulib
Packages required:
- autoconf
- automake
- gettext
- autopoint
- m4
- wget

Run `./ports/gnulib.sh`.

### coreutils
Packages required:
- bison
- gperf
- texinfo

Dependencies:
- gnulib

Run `./ports/coreutils.sh`.

### lua
Run `./ports/lua.sh`.

### figlet
Run `./ports/figlet.sh`.

### doomgeneric
Get an [IWAD](https://archive.org/details/theultimatedoom_doom2_doom.wad), then run `./ports/doomgeneric.sh`.

### fastfetch
Packages required:
- cmake

Run `./ports/fastfetch.sh`.

### ncurses
Run `./ports/ncurses.sh`.

### vim
Dependencies:
- ncurses

Run `./ports/vim.sh`.

### nyancat
Run `./ports/nyancat.sh`.

## Building the kernel
### x86_64
Packages required:
- git
- gmake
- gcc
- binutils
- xorriso
- nasm
- genext2fs

First run `make kernel-deps` to get the dependencies, then run `make run -j$(nproc)` to run it in QEMU.

### aarch64
Packages required:
- git
- gmake
- aarch64-none-elf-gcc
- aarch64-none-elf-binutils
- xorriso

Run `make kernel-deps` to get the dependencies, then run `make run -j$(nproc) ARCH=aarch64` to run it in QEMU.

## Screenshots
<img width="1154" height="926" alt="image" src="https://github.com/user-attachments/assets/c18b1f3e-f838-4839-a352-ecd221ba8f36" />
<img width="1154" height="926" alt="image" src="https://github.com/user-attachments/assets/c643893e-ee9d-4128-b287-bb40586367c8" />
<img width="1154" height="926" alt="image" src="https://github.com/user-attachments/assets/dc871600-422f-437b-9314-55be789b59a5" />
