# bentobox
bentobox is a 64-bit SMP-enabled operating system targeting x86_64 and aarch64.

## Features
- SMP capable preemptive modular kernel with full multi-architecture support
- POSIX compatibility
- IPC: Pipes, UNIX domain sockets, signals
- Ports: Xorg, bash, coreutils, vim, fastfetch & more
- Filesystems: ext2, devfs, tmpfs, procfs
- Block devices: AHCI
- Interrupt controllers: APIC (x86_64), GICv2 (aarch64)
- ACPI table parsing & full ACPI mode using uACPI
- PCI & PCIe support
- Input devices: PS/2 (x86_64), virtio-input (aarch64)
- Elf64 modules & binaries, VMM with CoW support

## Building the toolchain
Packages required:
- git
- gcc
- binutils
- base-devel

Start by sourcing the environment. Run `. build/mlibc-root`.

> [!NOTE]
> To build an aarch64 toolchain, run `. build/mlibc-root aarch64` instead.

Now build binutils. Run `./util/binutils.sh`.

Then, install mlibc headers for the GCC cross compiler. Run `make -f build/mlibc.mk headers`.

Now you can build the GCC cross compiler. Run `./util/gcc.sh`.

## Building the userspace
Packages required:
- git
- meson
- rsync

Start by building mlibc. Run `make -f build/mlibc.mk resetup build install`.

> [!NOTE]
> To build mlibc for aarch64, append `ARCH=aarch64` to the make command.

Afterwards, rebuild libgcc. Run `./util/libgcc.sh`.

Now proceed to build the ports below.

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
Run `./ports/doomgeneric.sh`.

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

### Xorg
Run `./ports/Xorg.sh` to build all Xorg-related libraries and `xorg-server`.

### twm
Dependencies:
- Xorg

Run `./ports/twm.sh`.

### xev
Dependencies:
- Xorg

Run `./ports/xev.sh`.

### xeyes
Dependencies:
- Xorg

Run `./ports/xeyes.sh`.

### xclock
Dependencies:
- Xorg

Run `./ports/xclock.sh`.

### nes_emu
Dependencies:
- Xorg

> [!NOTE]
> This port is unstable when running the kernel with SMP.

Run `./ports/nes_emu.sh`.

### dwm
Dependencies:
- Xorg

> [!NOTE]
> This port is currently broken.

Run `./ports/dwm.sh`.

### ttf-ibm-plex
Run `./ports/ttf-ibm-plex`.

### st
Dependencies:
- Xorg
- ttf-ibm-plex

Run `./ports/st.sh`.

## Building the kernel
### x86_64
Packages required:
- git
- gmake
- xorriso
- nasm
- qemu-system-x86

First run `make kernel-deps` to get the dependencies, then run `make run -j$(nproc)` to run it in QEMU.

### aarch64
Packages required:
- git
- gmake
- xorriso
- qemu-system-aarch64

Run `make kernel-deps` to get the dependencies, then run `make run -j$(nproc) ARCH=aarch64` to run it in QEMU.

## Screenshots
<img width="1154" height="926" alt="image" src="https://github.com/user-attachments/assets/c18b1f3e-f838-4839-a352-ecd221ba8f36" />
<img width="1154" height="926" alt="image" src="https://github.com/user-attachments/assets/239b33eb-6ab2-4b9e-8823-98448d572106" />
<img width="1154" height="926" alt="image" src="https://github.com/user-attachments/assets/2289bfdc-362d-4337-bb1b-86c034ab8924" />

