.SUFFIXES:

ARCH := x86_64
QEMUFLAGS := -m 2G -smp 4
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
	@qemu-system-$(ARCH) -M q35 -drive if=pflash,unit=0,format=raw,file=build/ovmf/ovmf-code-$(ARCH).fd,readonly=on -cdrom $(IMAGE_NAME).iso $(QEMUFLAGS)

.PHONY: run-bios
run-bios: $(IMAGE_NAME).iso
	@qemu-system-$(ARCH) -M q35 -cdrom $(IMAGE_NAME).iso -boot d $(QEMUFLAGS)

build/ovmf/ovmf-code-$(ARCH).fd:
	mkdir -p build/ovmf
	curl -Lo $@ https://github.com/osdev0/edk2-ovmf-nightly/releases/latest/download/ovmf-code-$(ARCH).fd

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
	@mkdir -p iso_root/boot/limine
	@cp boot/limine.conf iso_root/boot/limine/
	@mkdir -p iso_root/EFI/BOOT
	@cp build/limine/limine-bios.sys build/limine/limine-bios-cd.bin build/limine/limine-uefi-cd.bin iso_root/boot/limine/
	@cp build/limine/BOOTX64.EFI iso_root/EFI/BOOT/
	@cp build/limine/BOOTIA32.EFI iso_root/EFI/BOOT/
	@xorriso -as mkisofs -quiet -R -r -J -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
		-apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(IMAGE_NAME).iso > /dev/null 2>&1
	@./build/limine/limine bios-install $(IMAGE_NAME).iso 2>&1 >/dev/null | grep -Ev \
		"Physical|Installing|Secondary|partition|Stage|Reminder|Limine|directories|Converting|Conversion" | cat
	@rm -rf iso_root