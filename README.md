# bentobox
bentobox is a 64-bit SMP-enabled operating system targeting x86_64 and aarch64.

The kernel features a modular, portable monolithic design with a syscall set consisting of only the essentials.

The userspace is built on [mlibc](https://github.com/managarm/mlibc/), a portable standard library that provides a clean abstraction layer bentobox plugs into.

## Tested build environments
- Debian Trixie
- Arch Linux (latest rolling release)
- Ubuntu 22.04 LTS
- Ubuntu 25.04

## Building the userspace (x86_64)
Packages required:
- git
- meson
- clang

Start by building mlibc. Run `make -f build/mlibc.mk setup build install`.

Now you can build the ports. Run `. build/mlibc-root` to source the environment.

### bash
Run `./ports/bash.sh`.

### coreutils
Packages required:
- autoconf
- automake
- gettext
- bison
- gperf
- m4
- texinfo
- wget
- autopoint

Run `./ports/coreutils.sh`.

### lua
Run `./ports/lua.sh`.

## Building the kernel (x86_64)
Packages required:
- git
- make
- gcc
- binutils
- xorriso
- nasm

Run `make kernel-deps` to get the dependencies, and then you can use `make run -j$(nproc)` to run it in QEMU.

## Building the kernel (aarch64)
Packages required:
- git
- make
- aarch64-none-elf-gcc
- aarch64-none-elf-binutils
- xorriso

Run `make kernel-deps` to get the dependencies, and then you can use `make run ARCH=aarch64 TOOLCHAIN_PREFIX=aarch64-none-elf- -j$(nproc)` to run it in QEMU.
