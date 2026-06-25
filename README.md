# bentobox
bentobox is a 64-bit SMP-enabled operating system targeting x86_64 and aarch64, on which I work on in my free time.

## Features
- SMP capable preemptive modular kernel with full multi-architecture support
- POSIX compatibility
- IPC: Pipes, UNIX domain sockets, PTYs, signals
- Ports: Xorg, openbox, fvwm3, gcc, st, bash, coreutils, vim, fastfetch & more
- Filesystems: ext2, devfs, tmpfs, ustar, procfs
- Block devices: AHCI
- Interrupt controllers: APIC (x86_64), GICv2 (aarch64)
- ACPI table parsing & full ACPI mode using [uACPI](https://github.com/uACPI/uACPI)
- PCI(e) support
- Input devices: PS/2 (x86_64), virtio-input (aarch64), Wacom W8001 penabled touchscreen
- Elf64 modules & binaries, dynamic linking, CoW

## Building the userspace
Packages required:
- build-essential
- git
- pkg-config

Start by running `make jinx` to download and patch [jinx](https://codeberg.org/mintsuki/jinx).

Then `cd bootstrap/build-x86_64` (or `build-aarch64` if targeting aarch64), and run `jinx host-build '*'` to build the toolchain. This will also build [mlibc](https://github.com/managarm/mlibc) and its headers as `gcc` and `libstdc++-v3` require them.

Now you can build the base system. Run `jinx build base` to build a minimal system (or `jinx build '*'` to build a full distro), followed by `jinx install base base` (or `jinx install base '*'` if building a full distro).

> [!NOTE]
> Building a full distro might take some time; it could be a good time to make some coffee!

If you do not want to build a full distro but still get extra apps like Xorg, simply `jinx install base [package]` (package names in `bootstrap/recipes`).

Finally, run `make hdd -j$(nproc)` to make a hard disk image (or `make livecd -j$(nproc)` if you prefer an initrd).

## Building the kernel
### x86_64
Packages required:
- build-essential
- git
- xorriso
- nasm
- qemu-system-x86

To build and run bentobox in QEMU run `make run -j$(nproc)`. If your machine doesn't support KVM append `QEMUFLAGS="-display sdl` to the make command.

Otherwise, run `make -j$(nproc)` to build the kernel and write `bin/x86_64/image.iso` to a USB drive and give it a try on real hardware!

### aarch64
Packages required:
- build-essential
- git
- xorriso
- qemu-system-aarch64

Run `make run -j$(nproc) ARCH=aarch64` to run the kernel and run it in QEMU.

## Screenshots
<p><img src="https://github.com/user-attachments/assets/c18b1f3e-f838-4839-a352-ecd221ba8f36" alt="image"></p>
<p><img src="https://github.com/user-attachments/assets/239b33eb-6ab2-4b9e-8823-98448d572106" alt="image"></p>
<p><img src="https://github.com/user-attachments/assets/7f6039bf-ec7b-4736-81cd-9082116dfe0b" alt="image"></p>
<p><img src="https://github.com/user-attachments/assets/66daf770-700f-4c75-a61c-a1fae3b575d0" alt="image"></p>
