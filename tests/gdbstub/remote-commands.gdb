file build/puzzle.elf
target remote :1234
break *0x10700
continue
print $pc
del 1
stepi
stepi
continue
