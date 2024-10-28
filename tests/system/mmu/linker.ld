OUTPUT_ARCH( "riscv" )

ENTRY(_start)

SECTIONS
{
  . = 0x00000000;
  .text.main : { *(.text.main) }
  . = ALIGN(0x1000);
  .mystring : { *(.mystring) }
  .data.main : { *(.data.main) }
  .bss.main : { *(.bss.main) }

  .text : {
    . = 0x7fffeffc;
    *(.text.setup)
    *(.text.vm_setup)
  }
  . = ALIGN(0x1000);
  .data : {
    *(.data.setup)
    *(.data.vm_setup)
  }
  .bss : {
    *(.bss.setup)
    *(.bss.vm_setup)
  }

  . = ALIGN(0x1000);
  _end = .;
}
