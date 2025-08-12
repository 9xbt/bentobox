ARCH ?= x86_64
IMAGE_NAME = image

ifeq ($(ARCH),x86_64)
    AS := nasm
    CC := clang
    LD := ld
    ARCH_DIR := kernel/arch/x86_64
    ASFLAGS := -f elf64 -g -F dwarf
    CCFLAGS := -O2 -m64 -std=gnu11 -g -ffreestanding -Wall -Wextra -Wshadow -Wuninitialized -Wstrict-aliasing -nostdlib -Ibase/usr/include/ -Ilib/ -fno-stack-protector -Wno-unused-parameter -fno-stack-check -fno-lto -mno-red-zone -mno-80387 -mno-sse -mno-sse2 -fno-strict-aliasing -fno-optimize-sibling-calls
    LDFLAGS := -m elf_x86_64 -Tkernel/arch/x86_64/linker.ld -z noexecstack
    QEMUFLAGS := -serial stdio -cdrom bin/$(IMAGE_NAME).iso -boot d -M q35 -drive file=bin/$(IMAGE_NAME).hdd,format=raw,if=none,id=hdd0 -device ahci,id=ahci -device ide-hd,drive=hdd0,bus=ahci.0 -rtc base=localtime
    QEMUDISPLAY ?=
else ifeq ($(ARCH),riscv64)
    AS := riscv64-elf-as
    CC := riscv64-elf-gcc
    LD := riscv64-elf-ld
    ARCH_DIR := kernel/arch/riscv
    ASFLAGS :=
    CCFLAGS := -mcmodel=medany -ffreestanding -Wall -Wextra -nostdlib -Ibase/usr/include/ -fno-stack-protector -Wno-unused-parameter -fno-stack-check -fno-lto
    LDFLAGS := -m elf64lriscv -Tkernel/arch/riscv/linker.ld -z noexecstack
    QEMUFLAGS := -machine virt -bios none -kernel bin/image.elf -mon chardev=mon0,mode=readline,id=mon0 -chardev null,id=mon0 -display gtk
else
    $(error Unsupported architecture: $(ARCH))
endif

KERNEL_S_SOURCES := $(shell find kernel -type f -name '*.S' ! -path "kernel/arch/*")
KERNEL_C_SOURCES := $(shell find kernel -type f -name '*.c' ! -path "kernel/arch/*") $(shell find lib/flanterm -type f -name '*.c')
MODULE_C_SOURCES := $(shell find modules -type f -name '*.c')
ARCH_S_SOURCES   := $(shell find $(ARCH_DIR) -type f -name '*.S' | sed 's|^\./||')
ARCH_C_SOURCES   := $(shell find $(ARCH_DIR) -type f -name '*.c' | sed 's|^\./||')

KERNEL_OBJS := $(addprefix bin/, $(KERNEL_S_SOURCES:.S=.S.o) $(ARCH_S_SOURCES:.S=.S.o) $(KERNEL_C_SOURCES:.c=.c.o) $(ARCH_C_SOURCES:.c=.c.o))
MODULE_OBJS := $(addprefix bin/, $(MODULE_C_SOURCES:.c=.ko))

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

bin/lib/%.c.o: lib/%.c
	@echo " CC $<"
	@mkdir -p "$$(dirname $@)"
	@$(CC) $(CCFLAGS) -c $< -o $@

bin/modules/%.ko: modules/%.c
	@echo " CC $<"
	@mkdir -p "$$(dirname $@)"
	@$(CC) $(CCFLAGS) -mcmodel=large -fno-pic -c $< -o $@

kernel: $(KERNEL_OBJS)
	@echo " LD bin/image.elf"
	@$(LD) $(LDFLAGS) $^ -o bin/image.elf
	@objcopy --strip-debug bin/image.elf bin/ksym.elf

modules: $(MODULE_OBJS)

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
	@find bin/modules/ -type f -name '*.ko' -exec cp {} iso_root/modules/ \;
	@cp bin/image.elf iso_root/boot/image.elf
	@cp bin/ksym.elf iso_root/boot/ksym.elf
	@cp boot/grub.cfg iso_root/boot/grub/grub.cfg
	@grub-mkrescue -o bin/$(IMAGE_NAME).iso iso_root/ -quiet 2>&1 | grep -v libburnia >/dev/null 2>&1
	@rm -rf iso_root/
else ifeq ($(ARCH),riscv64)
iso:
else
    $(error Unsupported architecture: $(ARCH))
endif

bin/$(IMAGE_NAME).hdd: $(shell find base -type f) $(shell find apps -type f) | apps
	@echo " HD bin/$(IMAGE_NAME).hdd"
#	@cp -r base bin/
	@rsync -a --no-times --no-o --no-g base/ bin/base/
	@mkdir -p root
	@rsync -a --no-times --no-o --no-g root/ bin/base/
	@genext2fs -d bin/base -b 131072 -L bentobox bin/root.hdd 2>&1 >/dev/null | grep -v copying | cat
	@dd if=/dev/zero of=bin/$(IMAGE_NAME).hdd bs=1M count=128 status=none
	@parted -s bin/$(IMAGE_NAME).hdd mklabel gpt
	@parted -s bin/$(IMAGE_NAME).hdd mkpart primary ext2 1MiB 127MiB
	@parted -s bin/$(IMAGE_NAME).hdd name 1 bentobox
	@dd if=bin/root.hdd of=bin/$(IMAGE_NAME).hdd bs=512 seek=2048 conv=notrunc status=none
	@rm bin/root.hdd

hdd: apps bin/$(IMAGE_NAME).hdd

.PHONY: chroot
chroot:
	@sudo chroot base /usr/bin/bash -c "export PATH=\$PATH:/bin; exec /usr/bin/bash -i"

.PHONY: clean
clean:
	@rm -f $(BOOT_OBJS) $(KERNEL_OBJS)
	@rm -rf bin
