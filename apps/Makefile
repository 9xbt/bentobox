# Toolchain
AS = nasm
CC = musl-gcc
LD = ld

# Automatically find sources
C_SRCS = $(wildcard *.c)
S_SRCS = $(wildcard *.S)
C_EXES = $(C_SRCS:.c=)
S_EXES = $(S_SRCS:.S=)

# Flags
CFLAGS = -static -g
ASFLAGS = -f elf64
LDFLAGS =

# Output directory
OUT = ../bin/base/bin

all: dirs $(addprefix $(OUT)/, $(S_EXES)) $(addprefix $(OUT)/, $(C_EXES))

dirs:
	@mkdir -p $(OUT)

$(OUT)/%: %.c
	@echo " CC $<"
	@$(CC) $(CFLAGS) $< -o $@

$(OUT)/%: %.S
	@echo " AS $<"
	@$(AS) $(ASFLAGS) -o $@.o $<
	@$(LD) $(LDFLAGS) $@.o -o $@
	@rm $@.o

.PHONY: all clean