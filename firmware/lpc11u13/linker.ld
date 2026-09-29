ENTRY(Reset_Handler)
MEMORY {
    FLASH (rx) : ORIGIN = 0, LENGTH = 24K
    RAM (rwx) : ORIGIN = 0x10000000, LENGTH = 4K
}
_stack_top = ORIGIN(RAM) + LENGTH(RAM);
SECTIONS {
    .vectors : { KEEP(*(.vectors)) } > FLASH
    .crp 0x2fc : { LONG(0xffffffff) } > FLASH
    .text 0x300 : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
    .data : AT(LOADADDR(.text) + SIZEOF(.text)) {
        . = ALIGN(4); _data_start = .;
        *(.data*)
        /* A nonempty section keeps objcopy from using an empty RAM LMA. */
        BYTE(0); . = ALIGN(4); _data_end = .;
    } > RAM
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : {
        . = ALIGN(4); _bss_start = .;
        *(.bss*) *(COMMON) . = ALIGN(4); _bss_end = .;
    } > RAM
    /DISCARD/ : { *(.comment*) *(.ARM.exidx*) *(.ARM.extab*) *(.eh_frame*) }
}
ASSERT(_bss_end <= _stack_top - 2048, "Reserve at least 2 KiB for the stack")
