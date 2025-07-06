PHDRS
{
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = text_ld;
    . (NOLOAD) : { bin/ksym.o(.text) }
    . = rodata_ld;
    . (NOLOAD) : { bin/ksym.o(.rodata) }
    . = data_ld;
    . (NOLOAD) : { bin/ksym.o(.data) }
    . = bss_ld;
    . (NOLOAD) : {
        bin/ksym.o(COMMON)
        bin/ksym.o(.bss)
    }

    . = load_addr;
    .text : { bin/module.elf(.text) } :text
    . = ALIGN(4K);
    .rodata : { bin/module.elf(.rodata) } :rodata
    . = ALIGN(4K);
    .data : { bin/module.elf(.data) } :data
    . = ALIGN(4K);
    .bss : { bin/module.elf(.bss) } :data
}