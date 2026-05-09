.SUFFIXES:

QEMUFLAGS := -display sdl -accel kvm

ARCH := x86_64
IMAGE_NAME := bin/$(ARCH)/image

CFLAGS += -m64 -march=x86-64 -mabi=sysv -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel
LDFLAGS += -m elf_x86_64
NASMFLAGS := -f elf64 -g -F dwarf

HOST_CC := cc
HOST_CFLAGS := -g -O2 -pipe
HOST_CPPFLAGS :=
HOST_LDFLAGS :=
HOST_LIBS :=

.PHONY: all
all: $(IMAGE_NAME).iso

.PHONY: run
run: build/ovmf/ovmf-code-$(ARCH).fd $(IMAGE_NAME).iso
	@qemu-system-$(ARCH) -M q35 -drive if=pflash,unit=0,format=raw,file=build/ovmf/ovmf-code-$(ARCH).fd,readonly=on -cdrom $(IMAGE_NAME).iso -m 2G -smp 2 -serial stdio $$( [ -f "$(IMAGE_NAME).hdd" ] && echo "-drive file=$(IMAGE_NAME).hdd,format=raw" ) $(QEMUFLAGS)

.PHONY: run-bios
run-bios: $(IMAGE_NAME).iso
	@qemu-system-$(ARCH) -M q35 -cdrom $(IMAGE_NAME).iso -boot d -m 2G -smp 2 -serial stdio $$( [ -f "$(IMAGE_NAME).hdd" ] && echo "-drive file=$(IMAGE_NAME).hdd,format=raw" ) $(QEMUFLAGS)

$(IMAGE_NAME).iso: build/limine/limine kernel
	@rm -rf iso_root
	@mkdir -p iso_root/boot
	@cp bin/$(ARCH)/kernel iso_root/boot/
	@cp bin/$(ARCH)/initrd.tar.zst iso_root/boot/ 2>/dev/null || true
	@cp -r obj/$(ARCH)/modules/* iso_root/boot/ 2>/dev/null || true
	@mkdir -p iso_root/boot/limine
	@if [ -f bin/$(ARCH)/initrd.tar.zst ]; then \
		{ echo "default_entry: 2"; grep -v '^default_entry:' build/limine.conf; } > iso_root/boot/limine/limine.conf; \
	else \
		cp build/limine.conf iso_root/boot/limine/; \
	fi
	@mkdir -p iso_root/EFI/BOOT
	@cp build/limine/limine-bios.sys build/limine/limine-bios-cd.bin build/limine/limine-uefi-cd.bin iso_root/boot/limine/
	@cp build/limine/BOOTX64.EFI iso_root/EFI/BOOT/
	@cp build/limine/BOOTIA32.EFI iso_root/EFI/BOOT/
	@xorriso -as mkisofs -quiet -R -r -J -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
		-apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(IMAGE_NAME).iso > /dev/null 2>&1
	@./build/limine/limine bios-install $(IMAGE_NAME).iso >/dev/null 2>&1
	@rm -rf iso_root