ARCH ?= x86_64
QEMUDISPLAY ?=

# Output image name
IMAGE_NAME = image

# Architecture specific
ifeq ($(ARCH),x86_64)
	AS = nasm
	CC = clang
	LD = ld
    ARCH_DIR := kernel/arch/x86_64
    ASFLAGS := -f elf64 -g -F dwarf
    CCFLAGS := -O2 -m64 -std=gnu11 -g -ffreestanding -Wall -Wextra -Wshadow -Wuninitialized -Wstrict-aliasing -nostdlib -Ibase/usr/include/ -fno-stack-protector -Wno-unused-parameter -fno-stack-check -fno-lto -mno-red-zone -mno-80387 -mno-sse -mno-sse2
    LDFLAGS := -m elf_x86_64 -Tkernel/arch/x86_64/linker.ld -z noexecstack
    QEMUFLAGS := -serial stdio -cdrom bin/$(IMAGE_NAME).iso -boot d -M q35 -drive file=bin/$(IMAGE_NAME).hdd,format=raw,if=none,id=hdd0 -device ahci,id=ahci -device ide-hd,drive=hdd0,bus=ahci.0
else ifeq ($(ARCH),riscv64)
	AS = riscv64-elf-as
	CC = riscv64-elf-gcc
	LD = riscv64-elf-ld
    ARCH_DIR := kernel/arch/riscv
    ASFLAGS :=
    CCFLAGS := -mcmodel=medany -ffreestanding -Wall -Wextra -nostdlib -Ibase/usr/include/ -fno-stack-protector -Wno-unused-parameter -fno-stack-check -fno-lto
    LDFLAGS := -m elf64lriscv -Tkernel/arch/riscv/linker.ld -z noexecstack
    QEMUFLAGS := -machine virt -bios none -kernel bin/image.elf -mon chardev=mon0,mode=readline,id=mon0 -chardev null,id=mon0 -display gtk
else
    $(error Unsupported architecture: $(ARCH))
endif

# Automatically find sources
KERNEL_S_SOURCES := $(shell find kernel -type f -name '*.S' ! -path "kernel/arch/*")
KERNEL_C_SOURCES := $(shell find kernel -type f -name '*.c' ! -path "kernel/arch/*")
MODULE_C_SOURCES := $(shell find modules -type f -name '*.c')
ARCH_S_SOURCES   := $(shell find $(ARCH_DIR) -type f -name '*.S' | sed 's|^\./||')
ARCH_C_SOURCES   := $(shell find $(ARCH_DIR) -type f -name '*.c' | sed 's|^\./||')

# Get object files
KERNEL_OBJS := $(addprefix bin/, $(KERNEL_S_SOURCES:.S=.S.o) $(ARCH_S_SOURCES:.S=.S.o) $(KERNEL_C_SOURCES:.c=.c.o) $(ARCH_C_SOURCES:.c=.c.o))
MODULE_OBJS := $(addprefix bin/, $(MODULE_C_SOURCES:.c=.o))

# Get module binaries
MODULE_BINARIES := $(addprefix bin/, $(MODULE_C_SOURCES:.c=.elf))

# Module base load address
LOAD_ADDR := 0xFFFFFFFF80000000

.PHONY: all
all: kernel modules apps iso hdd

.PHONY: run
run: all
	@qemu-system-$(ARCH) $(QEMUFLAGS) $(QEMUDISPLAY) #-no-reboot -no-shutdown -d int -M smm=off

.PHONY: run-kvm
run-kvm: all
	@qemu-system-$(ARCH) $(QEMUFLAGS) $(QEMUDISPLAY) -accel kvm -smp $(shell expr $$(nproc) / 2)

.PHONY: run-gdb
run-gdb: all
	@qemu-system-$(ARCH) $(QEMUFLAGS) $(QEMUDISPLAY) -S -s

.PHONY: apps
apps:
	@$(MAKE) -C apps

bin/kernel/%.c.o: kernel/%.c
	@echo " CC $<"
	@mkdir -p "$$(dirname $@)"
	@$(CC) $(CCFLAGS) -DGIT_COMMIT_HASH=\"$(shell git describe --always --dirty)\" -c $< -o $@

bin/kernel/%.S.o: kernel/%.S
	@echo " AS $<"
	@mkdir -p "$$(dirname $@)"
	@$(AS) $(ASFLAGS) -o $@ $<

bin/.target:
	mkdir -p "$$(dirname $@)"
	@touch $@

bin/modules/%.o: modules/%.c $(KERNEL_OBJS)
	@echo " CC $<"
	@mkdir -p "$$(dirname $@)"
	@$(CC) $(CCFLAGS) -mcmodel=large -c $< -o $@

.PHONY: kernel
kernel: $(KERNEL_OBJS)
	@echo " LD kernel/*"
	@$(LD) $(LDFLAGS) $^ -o bin/image.elf
	@$(LD) $(LDFLAGS) -r $^ -o bin/ksym.o

.PHONY: modules
modules: kernel $(MODULE_OBJS)
	@./util/modules.sh $(MODULE_OBJS)

.PHONY: iso
ifeq ($(ARCH),x86_64)
iso: kernel modules
	@grub-file --is-x86-multiboot2 ./bin/image.elf; \
	if [ $$? -eq 1 ]; then \
		echo " error: image.elf is not a valid multiboot2 file"; \
		exit 1; \
	fi
	@mkdir -p iso_root/boot/grub/
	@mkdir -p iso_root/modules/
	@find bin/modules/ -type f -name '*.elf' -exec cp {} iso_root/modules/ \;
	@cp bin/image.elf iso_root/boot/image.elf
	@cp bin/ksym.elf iso_root/boot/ksym.elf
	@cp boot/grub.cfg iso_root/boot/grub/grub.cfg
	@grub-mkrescue -o bin/$(IMAGE_NAME).iso iso_root/ -quiet 2>&1 >/dev/null | grep -v libburnia | cat
	@rm -rf iso_root/
else ifeq ($(ARCH),riscv64)
iso:
else
    $(error Unsupported architecture: $(ARCH))
endif

.PHONY: hdd
hdd: apps
	@echo "HD bin/$(IMAGE_NAME).hdd"
	@cp -r base bin/
	@[ ! -e bin/base/bin/bash ] && ln -s /usr/bin/bash bin/base/bin/bash || true
	@bash util/busybox.sh
#	@cp -r /opt/mlibc/include bin/base/usr/
	genext2fs -d bin/base -b 131072 -L bentobox bin/root.hdd 2>&1 >/dev/null | grep -v copying | cat
	dd if=/dev/zero of=bin/$(IMAGE_NAME).hdd bs=1M count=128
	parted -s bin/$(IMAGE_NAME).hdd mklabel gpt
	parted -s bin/$(IMAGE_NAME).hdd mkpart primary ext2 1MiB 127MiB
	parted -s bin/$(IMAGE_NAME).hdd name 1 bentobox
	dd if=bin/root.hdd of=bin/$(IMAGE_NAME).hdd bs=512 seek=2048 conv=notrunc
	rm bin/root.hdd

.PHONY: clean
clean:
	@rm -f $(BOOT_OBJS) $(KERNEL_OBJS)
	@rm -rf bin
