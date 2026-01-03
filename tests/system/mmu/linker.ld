OUTPUT_ARCH( "riscv" )

ENTRY(_start)

/* Memory Layout (compact - avoids 2GB sparse ELF problem):
 *
 * 0x00000000 - User code (.text.main) - intentionally unmapped at boot
 *              to trigger instruction fetch page fault
 * 0x00001000 - User data (.mystring, .data.main, .bss.main)
 * 0x00400000 - FREE_FRAME_BASE (see vm_setup.c) - page allocation pool
 * 0x00800000 - Kernel/supervisor code (.text.setup, .text.vm_setup)
 *              Mapped to VA 0x80000000 via mega page in l1pt[512]
 *
 * The page table setup in vm_boot() maps:
 * - l1pt[0]: L2 table for user VA 0x00000000 - 0x003FFFFF
 * - l1pt[512]: Mega page mapping physical _start to VA 0x80000000
 * - l1pt[1023]: Mega page mapping physical main to VA 0xFFC00000
 *               (kernel uses this to copy pages on fault)
 */

SECTIONS
{
  /* User space code - starts at address 0, triggers page fault on first access */
  . = 0x00000000;
  .text.main : { *(.text.main) }
  . = ALIGN(0x1000);
  .mystring : { *(.mystring) }
  .data.main : { *(.data.main) }
  .bss.main : { *(.bss.main) }

  /* Kernel/supervisor code - placed at 8 MiB (was 0x7fffeffc causing 2GB ELF) */
  . = 0x00800000;
  _kernel_start = .;
  .text : {
    _text_start = .;
    *(.text.setup)
    *(.text.vm_setup)
    _text_end = .;
  }
  . = ALIGN(0x1000);
  .rodata : {
    _rodata_start = .;
    *(.rodata*)
    _rodata_end = .;
  }
  . = ALIGN(0x1000);
  _data_start = .;
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

/* Ensure kernel fits within 4 MiB mega page identity mapping */
ASSERT((_end - _kernel_start) <= 0x400000, "Kernel exceeds 4 MiB identity map limit")
