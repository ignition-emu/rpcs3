/* The PS5 module's layout for the title's loader, which maps code (read and
 * execute), read-only data and data (read and write) on 16 KiB pages each,
 * from address 0, and takes no TLS segment. */
OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)

PHDRS
{
	code PT_LOAD FLAGS(5);
	constants PT_LOAD FLAGS(4);
	variables PT_LOAD FLAGS(6);
	dynamic PT_DYNAMIC FLAGS(6);
}

SECTIONS
{
	. = 0;
	.text : ALIGN(0x4000) { *(.text .text.*) *(.plt .plt.*) } :code

	. = ALIGN(0x4000);
	.rodata : { *(.rodata .rodata.*) } :constants
	.eh_frame_hdr : { KEEP(*(.eh_frame_hdr)) } :constants
	.eh_frame : { KEEP(*(.eh_frame)) } :constants
	.gcc_except_table : { *(.gcc_except_table .gcc_except_table.*) } :constants
	.note.gnu.build-id : { *(.note.gnu.build-id) } :constants
	.dynsym : { *(.dynsym) } :constants
	.gnu.hash : { *(.gnu.hash) } :constants
	.hash : { *(.hash) } :constants
	.dynstr : { *(.dynstr) } :constants
	.rela.dyn : { *(.rela.dyn) } :constants
	.rela.plt : { *(.rela.plt) } :constants

	. = ALIGN(0x4000);
	.data : { *(.data .data.*) } :variables
	.data.rel.ro : { *(.data.rel.ro .data.rel.ro.*) } :variables
	.got : { *(.got) *(.got.plt) } :variables
	.preinit_array : { KEEP(*(.preinit_array)) } :variables
	.init_array : { KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*))) KEEP(*(.init_array .ctors)) } :variables
	.fini_array : { KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*))) KEEP(*(.fini_array .dtors)) } :variables
	.dynamic : { *(.dynamic) } :variables :dynamic
	.bss : { *(.bss .bss.*) *(COMMON) } :variables
}
