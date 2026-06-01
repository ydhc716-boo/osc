/*
 * STM32G474VETx Flash Linker Script
 *
 * STM32G474VE:
 *   Flash: 512 KB (0x08000000 – 0x08080000)
 *   SRAM:  128 KB (0x20000000 – 0x20020000)
 *   CCM SRAM: 32 KB (0x10000000 – 0x10008000)
 */

MEMORY
{
    FLASH    (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    RAM      (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
    CCMRAM   (rw)  : ORIGIN = 0x10000000, LENGTH = 32K
}

/* Stack and heap sizes */
_estack     = ORIGIN(RAM) + LENGTH(RAM);    /* End of RAM */
_min_heap_size  = 0x200;   /* 512 bytes minimum */
_min_stack_size = 0x800;   /* 2 KB minimum */

SECTIONS
{
    /* ── Interrupt Vector Table ──────────────────────────────────────── */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    /* ── Code ─────────────────────────────────────────────────────────── */
    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)

        KEEP(*(.init))
        KEEP(*(.fini))

        . = ALIGN(4);
        _etext = .;
    } > FLASH

    /* ── Read-only data ───────────────────────────────────────────────── */
    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    /* ── ARM Extab / Exidx ────────────────────────────────────────────── */
    .ARM.extab :
    {
        . = ALIGN(4);
        *(.ARM.extab* .gnu.linkonce.armextab.*)
        . = ALIGN(4);
    } > FLASH

    .ARM.exidx :
    {
        . = ALIGN(4);
        PROVIDE(__exidx_start = .);
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        PROVIDE(__exidx_end = .);
        . = ALIGN(4);
    } > FLASH

    /* ── Preinit / Init / Fini arrays ─────────────────────────────────── */
    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH

    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH

    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH

    /* ── Data (initialized) ───────────────────────────────────────────── */
    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    /* ── BSS (uninitialized) ──────────────────────────────────────────── */
    . = ALIGN(4);
    _sbss = .;

    .bss :
    {
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /* ── Heap & Stack ─────────────────────────────────────────────────── */
    ._user_heap_stack :
    {
        . = ALIGN(8);
        PROVIDE(end = .);
        PROVIDE(_end = .);
        . = . + _min_heap_size;
        . = . + _min_stack_size;
        . = ALIGN(8);
    } > RAM

    /* ── CCM RAM (optional, for critical data) ────────────────────────── */
    .ccmram (NOLOAD) :
    {
        . = ALIGN(4);
        *(.ccmram)
        *(.ccmram*)
        . = ALIGN(4);
    } > CCMRAM
}
