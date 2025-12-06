.SUFFIXES:

OUTPUT := kernel
ARCH := x86_64

ifeq ($(ARCH),aarch64)
    TOOLCHAIN ?= aarch64-none-elf
endif
TOOLCHAIN_PREFIX :=
ifneq ($(TOOLCHAIN),)
    ifeq ($(TOOLCHAIN_PREFIX),)
        TOOLCHAIN_PREFIX := $(TOOLCHAIN)-
    endif
endif

CC := $(if $(TOOLCHAIN_PREFIX),$(TOOLCHAIN_PREFIX)gcc,cc)
LD := $(TOOLCHAIN_PREFIX)ld
CFLAGS += -g -O0 -fno-omit-frame-pointer -pipe -Wall -Wextra -Wshadow -std=gnu11 -nostdinc -ffreestanding -fno-stack-protector -fno-stack-check -fno-lto -fno-PIC -ffunction-sections -fdata-sections -DGIT_COMMIT_HASH=\"$(shell git describe --always --dirty)\"
CPPFLAGS := -I base/usr/include/ -I build/limine-protocol/include -I lib/flanterm/src -I lib/lai/include -isystem build/freestnd-c-hdrs/include -DLIMINE_API_REVISION=3 -MMD -MP
LDFLAGS += -nostdlib -static -z max-page-size=0x1000 -T kernel/arch/$(ARCH)/linker.ld

include build/${ARCH}.mk

SOURCES := $(shell find -L kernel cc-runtime/src -type f -not -path 'kernel/arch/*' 2>/dev/null | LC_ALL=C sort)
SOURCES += $(shell find -L kernel/arch/$(ARCH) -type f 2>/dev/null | LC_ALL=C sort)
SOURCES += $(shell find lib/flanterm -type f -name '*.c')
SOURCES += $(shell find lib/lai -type f -name '*.c')

CFILES := $(filter %.c,$(SOURCES))
ASFILES := $(filter %.S,$(SOURCES))
NASMFILES := $(filter %.asm,$(SOURCES))

OBJ := $(addprefix obj/$(ARCH)/,$(CFILES:.c=.c.o) $(ASFILES:.S=.S.o))
OBJ += $(addprefix obj/$(ARCH)/,$(NASMFILES:.asm=.asm.o))
HEADER_DEPS := $(addprefix obj/$(ARCH)/,$(CFILES:.c=.c.d) $(ASFILES:.S=.S.d))

MODULE_SOURCES := $(shell find modules -type f -name '*.c')
MODULE_OBJS := $(addprefix obj/$(ARCH)/, $(MODULE_SOURCES:.c=.ko))

APPS_CC := $(ARCH)-pc-bentobox-gcc

APPS_CFLAGS := -g -O2 -Ibase/usr/include/
APPS_LDFLAGS := -Wl,--start-group -Lbin/$(ARCH)/lib -l:list.a -l:compositor.a -Wl,--end-group
APPS_SOURCES := $(shell find apps -type f)

APPS_CFILES := $(filter %.c,$(APPS_SOURCES))
APPS_ASFILES := $(filter %.S,$(APPS_SOURCES))
APPS_NASMFILES := $(filter %.asm,$(APPS_SOURCES))

APPS_OBJS :=
APPS_EXECUTABLES := $(addprefix bin/$(ARCH)/,$(APPS_CFILES:.c=))

ifeq ($(ARCH),x86_64)
APPS_OBJS += $(addprefix obj/$(ARCH)/,$(APPS_NASMFILES:.asm=.o))
APPS_EXECUTABLES += $(addprefix bin/$(ARCH)/,$(APPS_NASMFILES:.asm=))
endif
ifeq ($(ARCH),aarch64)
APPS_OBJS += $(addprefix obj/$(ARCH)/,$(APPS_ASFILES:.S=.o))
APPS_EXECUTABLES += $(addprefix bin/$(ARCH)/,$(APPS_ASFILES:.S=))
endif

LIB_CFLAGS := -g -O2 -Ibase/usr/include/ -I$(CURDIR)/build/mlibc/$(ARCH)/include
LIB_CFILES := lib/list.c lib/compositor.c
LIB_OBJS = $(addprefix obj/$(ARCH)/,$(LIB_CFILES:.c=.o))
LIB_LIBS = $(addprefix bin/$(ARCH)/,$(LIB_CFILES:.c=.a))

.PHONY: all
all: $(IMAGE_NAME).iso

kernel-deps:
	@./build/get-deps
	@touch build/kernel-deps

.PHONY: kernel
kernel: kernel-deps bin/$(ARCH)/$(OUTPUT) $(MODULE_OBJS) 

.PHONY: hdd
hdd: $(LIB_LIBS) $(APPS_EXECUTABLES) $(IMAGE_NAME).hdd

.PHONY: livecd
livecd: $(LIB_LIBS) $(APPS_EXECUTABLES) bin/$(ARCH)/initrd.tar

$(APPS_EXECUTABLES): $(LIB_LIBS)

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

bin/$(ARCH)/apps/%: obj/$(ARCH)/apps/%.o
	@echo " LD $@"
	@mkdir -p "$(dir $@)"
	@$(LD) -nostdlib -static $< -o $@

bin/$(ARCH)/apps/%: apps/%.c
	@echo " CC $<"
	@mkdir -p "$$(dirname $@)"
	@$(APPS_CC) $(APPS_CFLAGS) $< $(APPS_LDFLAGS) -o $@

ifeq ($(ARCH),aarch64)
obj/$(ARCH)/apps/%.o: apps/%.S
	@echo " AS $<"
	@mkdir -p "$$(dirname $@)"
	@$(CC) -march=armv8-a -c $< -o $@
endif

ifeq ($(ARCH),x86_64)
obj/$(ARCH)/apps/%.o: apps/%.asm
	@echo " AS $<"
	@mkdir -p "$$(dirname $@)"
	@nasm -f elf64 -o $@ $<
endif

obj/$(ARCH)/lib/%.o: lib/%.c
	@echo " CC $< "
	@mkdir -p "$(dir $@)"
	@$(APPS_CC) $(LIB_CFLAGS) -c $< -o $@

bin/$(ARCH)/lib/%.a: $(LIB_OBJS)
	@echo " AR $@"
	@mkdir -p "$(dir $@)"
	@ar rcs $@ $^

bin/$(ARCH)/initrd.tar: $(shell find build/base/$(ARCH) -type f) $(shell find base -type f) $(shell find apps -type f) $(APPS_EXECUTABLES)
	@echo " HD $@"
	@mkdir -p "$(dir $@)"
	@mkdir -p bin/$(ARCH)/base/bin
	@cp -r base/* bin/$(ARCH)/base/
	@cp -r build/base/$(ARCH)/* bin/$(ARCH)/base/
	@find bin/$(ARCH)/apps -mindepth 1 -exec cp -rt bin/$(ARCH)/base/bin/ {} + 2>/dev/null || true
	@tar -C bin/$(ARCH)/base -cf $@ .

$(IMAGE_NAME).hdd: $(shell find build/base/$(ARCH) -type f) $(shell find base -type f) $(shell find apps -type f) $(APPS_EXECUTABLES)
	@echo " HD $@"
	@mkdir -p "$(dir $@)"
	@mkdir -p bin/$(ARCH)/base/bin
	@cp -r base/* bin/$(ARCH)/base/
	@cp -r build/base/$(ARCH)/* bin/$(ARCH)/base/
	@find bin/$(ARCH)/apps -mindepth 1 -exec cp -rt bin/$(ARCH)/base/bin/ {} + 2>/dev/null || true
# 	@truncate -s 4000M $@
# 	@mkfs.ext2 -b 1024 -O ^filetype -F $@
# 	@sudo mkdir -p /mnt/bentobox
# 	@sudo mount -o loop $@ /mnt/bentobox
# 	@sudo rsync -a --info=progress2 bin/$(ARCH)/base /mnt/bentobox/
# 	@sudo umount /mnt/bentobox
	@genext2fs -d bin/$(ARCH)/base -b 4194304 -L bentobox -N 20000 $@ 2>&1 >/dev/null | grep -v copying | cat

.PHONY: clean
clean:
	@rm -rf bin obj iso_root $(IMAGE_NAME).iso $(IMAGE_NAME).hdd

.PHONY: distclean
distclean:
	@rm -rf bin obj build/freestnd-c-hdrs build/cc-runtime build/limine-protocol iso_root *.iso *.hdd build/kernel-deps limine ovmf