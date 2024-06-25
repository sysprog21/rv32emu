OUTPUT_ARCH( "riscv" )

ENTRY(_start)

SECTIONS
{
  . = 0x10000;
  .text : { *(.text) }
  . = 0x8000003;
  .misdata : { *(.misdata) }
  . = 0x10000007;
  .mistext : { *(.mistext) }
  .data : { *(.data) }
  .bss : { *(.bss) }
}
