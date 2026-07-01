CC := $(ARCH)-pc-bentobox-gcc

CFLAGS := -g -O2 -Ibase/usr/include/
LDFLAGS :=

CFILES := $(wildcard *.c)

OBJS :=
EXECUTABLES := $(addprefix bin/,$(CFILES:.c=))
DESTDIR ?= /usr

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