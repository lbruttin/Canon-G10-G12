/*
 * libgcc.ld - folds the libgcc helpers the module needs into one .text.
 *
 * elf2flt only understands .text, .data and .bss, and CHDK's link script
 * gathers only .text proper. libgcc keeps __udivmoddi4 -- behind the
 * engine's one signed 64-bit division -- in a section of its own, which
 * elf2flt refuses. The unwind tables go: nothing on the camera unwinds.
 */
SECTIONS
{
    .text : { *(.text .text.*) }
    .data : { *(.rodata .rodata.* .data .data.*) }
    .bss  : { *(.bss .bss.* COMMON) }
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) }
}
