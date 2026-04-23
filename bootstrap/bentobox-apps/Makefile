CC := $(ARCH)-pc-bentobox-gcc

CFLAGS := -g -O2 -Ibase/usr/include/
LDFLAGS :=

CFILES := $(wildcard *.c)
ASFILES := $(wildcard *.S)
NASMFILES := $(wildcard *.asm)

OBJS :=
EXECUTABLES := $(addprefix bin/,$(CFILES:.c=))
DESTDIR ?= /usr

ifeq ($(ARCH),x86_64)
    OBJS += $(addprefix obj/,$(NASMFILES:.asm=.o))
    EXECUTABLES += $(addprefix bin/,$(NASMFILES:.asm=))
endif

ifeq ($(ARCH),aarch64)
    OBJS += $(addprefix obj/,$(ASFILES:.S=.o))
    EXECUTABLES += $(addprefix bin/,$(ASFILES:.S=))
endif

all: $(EXECUTABLES)

install:
	mkdir -p $(DESTDIR)/bin
	cp bin/* $(DESTDIR)/bin/

bin/%: obj/%.o
	@echo " LD $@"
	@mkdir -p "$(dir $@)"
	@$(LD) -nostdlib -static $< -o $@

bin/%: %.c
	@echo " CC $<"
	@mkdir -p "$$(dirname $@)"
	@$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

ifeq ($(ARCH),aarch64)
obj/%.o: %.S
	@echo " AS $<"
	@mkdir -p "$$(dirname $@)"
	@$(CC) -march=armv8-a -c $< -o $@
endif

ifeq ($(ARCH),x86_64)
obj/%.o: %.asm
	@echo " AS $<"
	@mkdir -p "$$(dirname $@)"
	@nasm -f elf64 -o $@ $<
endif