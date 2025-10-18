.SUFFIXES:

QEMUFLAGS :=

ARCH := aarch64
DEFAULT_QEMUFLAGS := -m 2G -smp 2
IMAGE_NAME := bin/$(ARCH)/image

CFLAGS += -mcpu=generic -march=armv8-a+nofp+nosimd -mgeneral-regs-only
LDFLAGS += -m aarch64elf

HOST_CC := cc
HOST_CFLAGS := -g -O2 -pipe
HOST_CPPFLAGS :=
HOST_LDFLAGS :=
HOST_LIBS :=

APPS_CC := aarch64-linux-gnu-gcc
APPS_LDFLAGS += -m aarch64elf

.PHONY: all
all: $(IMAGE_NAME).iso

run: build/ovmf/ovmf-code-$(ARCH).fd $(IMAGE_NAME).iso
	@qemu-system-$(ARCH) -M virt -cpu cortex-a72 -device ramfb -device qemu-xhci -device usb-kbd -device usb-mouse -serial stdio -drive if=pflash,unit=0,format=raw,file=build/ovmf/ovmf-code-$(ARCH).fd,readonly=on -cdrom $(IMAGE_NAME).iso $(DEFAULT_QEMUFLAGS) $(QEMUFLAGS)

build/ovmf/ovmf-code-$(ARCH).fd:
	mkdir -p build/ovmf
	curl -Lo $@ https://github.com/osdev0/edk2-ovmf-nightly/releases/latest/download/ovmf-code-$(ARCH).fd
	dd if=/dev/zero of=$@ bs=1 count=0 seek=67108864 2>/dev/null

build/limine/limine:
	rm -rf build/limine
	git clone https://github.com/limine-bootloader/limine.git build/limine --branch=v9.x-binary --depth=1
	$(MAKE) -C build/limine \
		CC="$(HOST_CC)" \
		CFLAGS="$(HOST_CFLAGS)" \
		CPPFLAGS="$(HOST_CPPFLAGS)" \
		LDFLAGS="$(HOST_LDFLAGS)" \
		LIBS="$(HOST_LIBS)"

$(IMAGE_NAME).iso: build/limine/limine kernel
	@rm -rf iso_root
	@mkdir -p iso_root/boot
	@cp bin/$(ARCH)/kernel iso_root/boot/
	@cp bin/$(ARCH)/initrd.tar iso_root/boot/
	@cp -r obj/$(ARCH)/modules/* iso_root/boot/
	@mkdir -p iso_root/boot/limine
	@cp build/limine.conf iso_root/boot/limine/
	@mkdir -p iso_root/EFI/BOOT
	@cp build/limine/limine-uefi-cd.bin iso_root/boot/limine/
	@cp build/limine/BOOTAA64.EFI iso_root/EFI/BOOT/
	@xorriso -as mkisofs -quiet -R -r -J \
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(IMAGE_NAME).iso > /dev/null 2>&1
	@rm -rf iso_root