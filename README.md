# bentobox
bentobox is a 64-bit SMP-enabled operating system targeting x86_64 and aarch64.

## Features
- SMP capable preemptive modular kernel with full multi-architecture support
- POSIX compatibility
- IPC: Pipes, UNIX domain sockets, PTYs, signals
- Ports: Xorg, st, bash, coreutils, vim, fastfetch & more
- Filesystems: ext2, devfs, tmpfs, procfs
- Block devices: AHCI
- Interrupt controllers: APIC (x86_64), GICv2 (aarch64)
- ACPI table parsing & full ACPI mode using [uACPI](https://github.com/uACPI/uACPI)
- PCI & PCIe support
- Input devices: PS/2 (x86_64), virtio-input (aarch64)
- Elf64 modules & binaries, VMM with CoW support

## Building the userspace
Packages required:
- build-essential
- git
- pkg-config

Start by running `make jinx` to download and patch [jinx](https://codeberg.org/mintsuki/jinx).

Then `cd bootstrap/build-x86_64` (or `build-aarch64` if targeting aarch64), and run `jinx host-build '*'` to build the toolchain. This will also build [mlibc](https://github.com/managarm/mlibc) and its headers as `gcc` and `libstdc++-v3` require them.

Now you can build the base system. Run `jinx build base` to build a minimal system (or `jinx build '*'` to build a full distro), followed by `jinx install base base` (or `jinx install base '*'` if building a full distro).

> [!NOTE]
> Building a full distro *will* take a long time; it might be a good time to make some coffee!

If you don't want to build a full distro but still get extra apps like Xorg, simply `jinx install base [package]` (package names in `bootstrap/recipes`).

Finally, run `make hdd -j$(nproc)` to make the HDD image (or `make livecd -j$(nproc)` if you prefer an initrd).

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
<p><img src="https://github.com/user-attachments/assets/da003f5a-bba1-4cf2-a492-7f55995f3329" alt="image"></p>
<p><img src="https://github.com/user-attachments/assets/bdfea070-c49b-49c3-bae4-af7e5ffc40c6" alt="image"></p>
<p><img src="https://github.com/user-attachments/assets/bc37c2b7-9d77-4f18-b06d-53616353a998" alt="image"></p>
