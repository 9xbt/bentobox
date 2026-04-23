.SUFFIXES:

OUTPUT := kernel
ARCH := x86_64
TOOLCHAIN :=

TOOLCHAIN_PREFIX :=
ifneq ($(TOOLCHAIN),)
    ifeq ($(TOOLCHAIN_PREFIX),)
        TOOLCHAIN_PREFIX := $(TOOLCHAIN)-
    endif
endif

GIT_HASH := $(shell git describe --always --dirty)

CC := $(if $(TOOLCHAIN_PREFIX),$(TOOLCHAIN_PREFIX)gcc,cc)
LD := $(TOOLCHAIN_PREFIX)ld
CFLAGS += -g -O2 -fno-omit-frame-pointer -fno-optimize-sibling-calls -pipe -Wall -Wextra -Wshadow -std=gnu11 -nostdinc -ffreestanding -fno-stack-protector -fno-stack-check -fno-lto -fno-PIC -ffunction-sections -fdata-sections -DGIT_COMMIT_HASH=\"$(GIT_HASH)\" -DFLANTERM_FB_DISABLE_BUMP_ALLOC
CPPFLAGS := -I base/usr/include/ -I build/limine-protocol/include -I lib/flanterm/src -I lib/uACPI/include -isystem build/freestnd-c-hdrs/include -DLIMINE_API_REVISION=3 -MMD -MP
LDFLAGS += -nostdlib -static -z max-page-size=0x1000 -T kernel/arch/$(ARCH)/linker.ld

include build/${ARCH}.mk

SOURCES := $(shell find -L kernel cc-runtime/src -type f -not -path 'kernel/arch/*' 2>/dev/null | LC_ALL=C sort)
SOURCES += $(shell find -L kernel/arch/$(ARCH) -type f 2>/dev/null | LC_ALL=C sort)
SOURCES += $(shell find lib/flanterm -type f -name '*.c')
SOURCES += $(shell find lib/uACPI/source -type f -name '*.c')

CFILES := $(filter %.c,$(SOURCES))
ASFILES := $(filter %.S,$(SOURCES))
NASMFILES := $(filter %.asm,$(SOURCES))

OBJ := $(addprefix obj/$(ARCH)/,$(CFILES:.c=.c.o) $(ASFILES:.S=.S.o))
OBJ += $(addprefix obj/$(ARCH)/,$(NASMFILES:.asm=.asm.o))
HEADER_DEPS := $(addprefix obj/$(ARCH)/,$(CFILES:.c=.c.d) $(ASFILES:.S=.S.d))

MODULE_SOURCES := $(shell find modules -type f -name '*.c')
MODULE_OBJS := $(addprefix obj/$(ARCH)/, $(MODULE_SOURCES:.c=.ko))

.PHONY: all
all: $(IMAGE_NAME).iso

.PHONY: kernel
kernel: bin/$(ARCH)/$(OUTPUT) $(MODULE_OBJS) 

.PHONY: hdd
hdd: $(LIB_LIBS) $(APPS_EXECUTABLES) $(IMAGE_NAME).hdd

.PHONY: livecd
livecd: $(LIB_LIBS) $(APPS_EXECUTABLES) bin/$(ARCH)/initrd.tar

-include $(HEADER_DEPS)

bin/$(ARCH)/$(OUTPUT): kernel/arch/$(ARCH)/linker.ld $(OBJ)
	@echo " LD $@"
	@mkdir -p "$(dir $@)"
	@$(LD) $(LDFLAGS) $(OBJ) -o $@

obj/$(ARCH)/%.c.o: %.c
	@echo " CC $<"
	@mkdir -p "$(dir $@)"
	@$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

obj/$(ARCH)/%.S.o: %.S
	@echo " AS $<"
	@mkdir -p "$(dir $@)"
	@$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

obj/$(ARCH)/%.asm.o: %.asm
	@echo " AS $<"
	@mkdir -p "$(dir $@)"
	@nasm $(NASMFLAGS) $< -o $@

obj/$(ARCH)/modules/%.ko: modules/%.c
	@echo " CC $<"
	@mkdir -p "$$(dirname $@)"
	@$(CC) $(CFLAGS) $(CPPFLAGS) -mcmodel=large -fno-pic -c $< -o $@

bin/$(ARCH)/initrd.tar: $(shell find bootstrap/build-$(ARCH)/base -type f) $(shell find base -type f)
	@echo " HD $@"
	@mkdir -p "$(dir $@)"
	@mkdir -p bin/$(ARCH)/base/bin
	@cp -r base/* bin/$(ARCH)/base/
	@cp -r bootstrap/build-$(ARCH)/base/* bin/$(ARCH)/base/
	@find bin/$(ARCH)/apps -mindepth 1 -exec cp -rt bin/$(ARCH)/base/bin/ {} + 2>/dev/null || true
	@tar -C bin/$(ARCH)/base -cf $@ .

$(IMAGE_NAME).hdd: $(shell find bootstrap/build-$(ARCH)/base -type f) $(shell find base -type f)
	@echo " HD $@"
	@mkdir -p "$(dir $@)"
	@mkdir -p bin/$(ARCH)/base/bin
	@cp -r base/* bin/$(ARCH)/base/
	@cp -r --no-preserve=mode bootstrap/build-$(ARCH)/base/* bin/$(ARCH)/base/
	@find bin/$(ARCH)/apps -mindepth 1 -exec cp -rt bin/$(ARCH)/base/bin/ {} + 2>/dev/null || true
	@truncate -s 4000M $@
	@mkfs.ext2 -b 1024 -O ^filetype -F $@
	@sudo mkdir -p /mnt/bentobox
	@sudo mount -o loop $@ /mnt/bentobox
	@sudo rsync -a --info=progress2 bin/$(ARCH)/base/* /mnt/bentobox/
	@sudo umount /mnt/bentobox

.PHONY: clean
clean:
	@rm -rf bin obj iso_root $(IMAGE_NAME).iso $(IMAGE_NAME).hdd

.PHONY: jinx
jinx: bootstrap/jinx
	mkdir -p bootstrap/build-$(ARCH)/sysroot
	ln -sf ../jinx bootstrap/build-$(ARCH)/jinx
	cd bootstrap/build-$(ARCH) && jinx init .. ARCH=$(ARCH)

build/limine/limine:
	rm -rf build/limine
	git clone https://github.com/limine-bootloader/limine.git build/limine --branch=v9.x-binary --depth=1
	$(MAKE) -C build/limine \
		CC="$(HOST_CC)" \
		CFLAGS="$(HOST_CFLAGS)" \
		CPPFLAGS="$(HOST_CPPFLAGS)" \
		LDFLAGS="$(HOST_LDFLAGS)" \
		LIBS="$(HOST_LIBS)"

bootstrap/jinx:
	curl -o $@ https://codeberg.org/Mintsuki/jinx/raw/commit/e6f44d1bd8c6a504fc3fbfcc16ddb549e2e89a3c/jinx
	chmod +x $@
	cd bootstrap && patch < ../build/jinx-remove-intree-check.diff