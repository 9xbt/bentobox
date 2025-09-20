# bentobox
bentobox is a 64-bit SMP-enabled operating system targeting x86_64 and aarch64.

## Building (x86_64)
```bash
bentobox $ make -f build/mlibc.mk setup build install   # build mlibc
bentobox $ make -j$(nproc)                              # build the kernel
bentobox $ make run                                     # run it!
```

## Building (aarch64)
> [!NOTE]
> aarch64 requires I. a GCC bare metal target cross compiler II. a GCC linux target cross compiler

```bash
bentobox $ make -f build/mlibc.mk setup build install ARCH=aarch64          # build mlibc
bentobox $ make -j$(nproc) ARCH=aarch64 TOOLCHAIN_PREFIX=aarch64-none-elf   # build the kernel
bentobox $ make run ARCH=aarch64 TOOLCHAIN_PREFIX=aarch64-none-elf          # run it!
```