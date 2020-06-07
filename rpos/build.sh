#!/bin/sh
~/src/llvm-project/out/gn/bin/clang --target=arm-none-eabi -march=armv7 -c kernel.s
~/src/llvm-project/out/gn/bin/clang --target=arm-none-eabi -T kernel.ld kernel.o -nostdlib -ffreestanding -o kernel.elf
~/src/llvm-project/out/gn/bin/llvm-objcopy kernel.elf -O binary kernel7.img


# -mgeneral-regs-only? -nostartfiles? -mcpu=cortex-a7? -fpic?
