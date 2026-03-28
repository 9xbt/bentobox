.SUFFIXES:

QEMUFLAGS := -display sdl

ARCH := aarch64
IMAGE_NAME := bin/$(ARCH)/image

CFLAGS += -mcpu=generic -march=armv8-a+nofp+nosimd -mgeneral-regs-only -mno-outline-atomics
LDFLAGS += -m aarch64elf

HOST_CC := cc
HOST_CFLAGS := -g -O2 -pipe
HOST_CPPFLAGS :=
HOST_LDFLAGS :=
HOST_LIBS :=

APPS_LDFLAGS += -m aarch64elf

.PHONY: all
all: $(IMAGE_NAME).iso

.PHONY: run
run: build/ovmf/ovmf-code-$(ARCH).fd $(IMAGE_NAME).iso
	@qemu-system-$(ARCH) -M virt -cpu cortex-a72 -device ramfb -device qemu-xhci -device usb-kbd -device virtio-keyboard-device -device virtio-tablet-device -global virtio-mmio.force-legacy=false -serial stdio -drive if=pflash,unit=0,format=raw,file=build/ovmf/ovmf-code-$(ARCH).fd,readonly=on -cdrom $(IMAGE_NAME).iso -m 2G -smp 2 $$( [ -f "$(IMAGE_NAME).hdd" ] && echo "-device ich9-ahci,id=sata -drive file=$(IMAGE_NAME).hdd,format=raw,if=none,id=disk0 -device ide-hd,drive=disk0,bus=sata.0" ) $(QEMUFLAGS)

$(IMAGE_NAME).iso: build/limine/limine kernel
	@rm -rf iso_root
	@mkdir -p iso_root/boot
	@cp bin/$(ARCH)/kernel iso_root/boot/
	@cp bin/$(ARCH)/initrd.tar iso_root/boot/ 2>/dev/null || true
	@cp -r obj/$(ARCH)/modules/* iso_root/boot/ 2>/dev/null || true
	@mkdir -p iso_root/boot/limine
	@if [ -f bin/$(ARCH)/initrd.tar ]; then \
		{ echo "default_entry: 2"; grep -v '^default_entry:' build/limine.conf; } > iso_root/boot/limine/limine.conf; \
	else \
		cp build/limine.conf iso_root/boot/limine/; \
	fi
	@mkdir -p iso_root/EFI/BOOT
	@cp build/limine/limine-uefi-cd.bin iso_root/boot/limine/
	@cp build/limine/BOOTAA64.EFI iso_root/EFI/BOOT/
	@xorriso -as mkisofs -quiet -R -r -J \
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(IMAGE_NAME).iso > /dev/null 2>&1
	@rm -rf iso_root