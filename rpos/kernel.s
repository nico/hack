// ARMv7
// ~/src/llvm-project/out/gn/bin/clang --target=arm-none-eabi -march=armv7 -c kernel.s
// ~/src/llvm-project/out/gn/bin/clang --target=arm-none-eabi -T kernel.ld kernel.o -nostdlib -ffreestanding -o kernel.elf
// ~/src/llvm-project/out/gn/bin/llvm-objcopy kernel.elf -O binary kernel7.img
// qemu-system-arm -M raspi2 -monitor stdio -kernel kernel.elf
// # `info registers` for `-monitor stdio` to see register values.
.section ".text.boot"

.globl _start
// .org 0x8000  // Don't use: linker script kernel.ld already does this.
_start:
  // Shut off extra cores
  mrc p15, 0, r5, c0, c0, 5
  and r5, r5, #3
  cmp r5, #0
  bne halt

  mov sp, #0x8000
  mov r0, #0x42
  ldr r1, =_start

halt:
  wfe
  b halt
