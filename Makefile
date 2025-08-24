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
CFLAGS += -g -O2 -pipe -Wall -Wextra -std=gnu11 -nostdinc -ffreestanding -fno-stack-protector -fno-stack-check -fno-lto -fno-PIC -ffunction-sections -fdata-sections
CPPFLAGS := -I base/usr/include/ -I build/limine-protocol/include -I lib/flanterm/src -isystem build/freestnd-c-hdrs/include -DLIMINE_API_REVISION=3 -MMD -MP
NASMFLAGS := $(patsubst -g,-g -F dwarf,$(NASMFLAGS)) -Wall -g
LDFLAGS += -nostdlib -static -z max-page-size=0x1000 --gc-sections -T kernel/arch/$(ARCH)/linker.ld

ifeq ($(ARCH),x86_64)
    CFLAGS += -m64 -march=x86-64 -mabi=sysv -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel
    LDFLAGS += -m elf_x86_64
    NASMFLAGS := -f elf64 $(NASMFLAGS)
else ifeq ($(ARCH),aarch64)
    CFLAGS += -mcpu=generic -march=armv8-a+nofp+nosimd -mgeneral-regs-only
    LDFLAGS += -m aarch64elf
else
	$(error Unsupported architecture: $(ARCH))
endif

SOURCES := $(shell find -L kernel cc-runtime/src -type f -not -path 'kernel/arch/*' 2>/dev/null | LC_ALL=C sort)
SOURCES += $(shell find -L kernel/arch/$(ARCH) -type f 2>/dev/null | LC_ALL=C sort)
SOURCES += $(shell find lib/flanterm -type f -name '*.c')

CFILES := $(filter %.c,$(SOURCES))
ASFILES := $(filter %.S,$(SOURCES))
NASMFILES := $(filter %.asm,$(SOURCES))

OBJ := $(addprefix obj/$(ARCH)/,$(CFILES:.c=.c.o) $(ASFILES:.S=.S.o))
OBJ += $(addprefix obj/$(ARCH)/,$(NASMFILES:.asm=.asm.o))
HEADER_DEPS := $(addprefix obj/$(ARCH)/,$(CFILES:.c=.c.d) $(ASFILES:.S=.S.d))

include build/${ARCH}.mk

.PHONY: all
all: $(IMAGE_NAME).iso

kernel-deps:
	@./build/get-deps
	@touch build/kernel-deps

.PHONY: kernel
kernel: kernel-deps bin/$(ARCH)/$(OUTPUT)

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

.PHONY: clean
clean:
	rm -rf bin obj iso_root $(IMAGE_NAME).iso $(IMAGE_NAME).hdd

.PHONY: distclean
distclean:
	rm -rf bin obj build/freestnd-c-hdrs build/cc-runtime build/limine-protocol iso_root *.iso *.hdd build/kernel-deps limine ovmf