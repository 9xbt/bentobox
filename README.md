# bentobox
bentobox is a 64-bit SMP-enabled operating system targeting x86_64 and aarch64.

## Tested build environments
- Debian Trixie
- Arch Linux (latest rolling release)
- Ubuntu 22.04 LTS
- Ubuntu 25.04

## Building the userspace
### x86_64
Packages required:
- git
- meson
- clang

Start by building mlibc. Run `make -f build/mlibc.mk setup build install`.

Now you can build the ports. Run `. build/mlibc-root` to source the environment.

### aarch64
Packages required:
- git
- meson
- aarch64-linux-gnu-gcc

Start by building mlibc. Run `make -f build/mlibc.mk setup build install ARCH=aarch64`.

Now you can build the ports. Run `. build/mlibc-root aarch64` and `TOOLCHAIN_PREFIX=aarch64-linux-gnu-` to source the environment.

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

### doomgeneric
Get an [IWAD](https://archive.org/details/theultimatedoom_doom2_doom.wad), then run `./ports/doomgeneric.sh`

## Building the kernel
### x86_64
Packages required:
- git
- make
- gcc
- binutils
- xorriso
- nasm

Run `make kernel-deps` to get the dependencies, and then you can use `make run -j$(nproc)` to run it in QEMU.

### aarch64
Packages required:
- git
- make
- aarch64-none-elf-gcc
- aarch64-none-elf-binutils
- xorriso

Run `make kernel-deps` to get the dependencies, and then you can use `make run ARCH=aarch64 TOOLCHAIN_PREFIX=aarch64-none-elf- -j$(nproc)` to run it in QEMU.
