#!/bin/bash
set -e

objcopy --only-keep-debug bin/image.elf bin/ksym.elf
readelf -S bin/ksym.elf | grep -w .text | head -n 1 | awk '{print "text_ld = 0x" $5 ";"}' > bin/sections.ld
readelf -S bin/ksym.elf | grep -w .rodata | head -n 1 | awk '{print "rodata_ld = 0x" $5 ";"}' >> bin/sections.ld
readelf -S bin/ksym.elf | grep -w .data | head -n 1 | awk '{print "data_ld = 0x" $5 ";"}' >> bin/sections.ld
readelf -S bin/ksym.elf | grep -w .bss | head -n 1 | awk '{print "bss_ld = 0x" $5 ";"}' >> bin/sections.ld
cat bin/sections.ld modules/linker.ld > bin/mod.ld

LOAD_ADDR=0xFFFFFFFF80000000
MODULE_OBJS=("$@")

for obj in "${MODULE_OBJS[@]}"; do
    echo " LD $obj"
    cp "$obj" bin/module.elf
    ld -Tbin/mod.ld --defsym=load_addr="$LOAD_ADDR" -o "${obj%.o}.elf"
    LOAD_ADDR=$(printf '0x%X' $((LOAD_ADDR + 0x1000000)))
done
