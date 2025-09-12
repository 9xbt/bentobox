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

CC := $(if $(TOOLCHAIN_PREFIX),$(TOOLCHAIN_PREFIX)gcc,cc)
LD := $(TOOLCHAIN_PREFIX)ld
CFLAGS += -g -O0 -pipe -Wall -Wextra -std=gnu11 -nostdinc -ffreestanding -fno-stack-protector -fno-stack-check -fno-lto -fno-PIC -ffunction-sections -fdata-sections -DGIT_COMMIT_HASH=\"$(shell git describe --always --dirty)\"
CPPFLAGS := -I base/usr/include/ -I build/limine-protocol/include -I lib/flanterm/src -isystem build/freestnd-c-hdrs/include -DLIMINE_API_REVISION=3 -MMD -MP
LDFLAGS += -nostdlib -static -z max-page-size=0x1000 --gc-sections -T kernel/arch/$(ARCH)/linker.ld

include build/${ARCH}.mk

SOURCES := $(shell find -L kernel cc-runtime/src -type f -not -path 'kernel/arch/*' 2>/dev/null | LC_ALL=C sort)
SOURCES += $(shell find -L kernel/arch/$(ARCH) -type f 2>/dev/null | LC_ALL=C sort)
SOURCES += $(shell find lib/flanterm -type f -name '*.c')

CFILES := $(filter %.c,$(SOURCES))
ASFILES := $(filter %.S,$(SOURCES))
NASMFILES := $(filter %.asm,$(SOURCES))

OBJ := $(addprefix obj/$(ARCH)/,$(CFILES:.c=.c.o) $(ASFILES:.S=.S.o))
OBJ += $(addprefix obj/$(ARCH)/,$(NASMFILES:.asm=.asm.o))
HEADER_DEPS := $(addprefix obj/$(ARCH)/,$(CFILES:.c=.c.d) $(ASFILES:.S=.S.d))

MODULE_SOURCES := $(shell find modules -type f -name '*.c')
MODULE_OBJS := $(addprefix obj/, $(MODULE_SOURCES:.c=.ko))

.PHONY: all
all: $(IMAGE_NAME).iso

kernel-deps:
	@./build/get-deps
	@touch build/kernel-deps

.PHONY: kernel
kernel: kernel-deps bin/$(ARCH)/$(OUTPUT) $(MODULE_OBJS) bin/$(ARCH)/initrd.tar

-include $(HEADER_DEPS)

bin/$(ARCH)/$(OUTPUT): kernel/arch/$(ARCH)/linker.ld $(OBJ)
	@echo "  LD $@"
	@mkdir -p "$(dir $@)"
	@$(LD) $(LDFLAGS) $(OBJ) -o $@

obj/$(ARCH)/%.c.o: %.c
	@echo "  CC $<"
	@mkdir -p "$(dir $@)"
	@$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

obj/$(ARCH)/%.S.o: %.S
	@echo "  AS $<"
	@mkdir -p "$(dir $@)"
	@$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

obj/$(ARCH)/%.asm.o: %.asm
	@echo "  AS $<"
	@mkdir -p "$(dir $@)"
	@nasm $(NASMFLAGS) $< -o $@

obj/modules/%.ko: modules/%.c
	@echo "  CC $<"
	@mkdir -p "$$(dirname $@)"
	@$(CC) $(CFLAGS) $(CPPFLAGS) -mcmodel=large -fno-pic -c $< -o $@

bin/$(ARCH)/initrd.tar: $(shell find base -type f)
	@echo " TAR $@"
	@mkdir -p "$(dir $@)"
	@tar -C base -cf $@ .

.PHONY: clean
clean:
	@rm -rf bin obj iso_root $(IMAGE_NAME).iso $(IMAGE_NAME).hdd

.PHONY: distclean
distclean:
	@rm -rf bin obj build/freestnd-c-hdrs build/cc-runtime build/limine-protocol iso_root *.iso *.hdd build/kernel-deps limine ovmf