#!/bin/sh
qemu-system-arm -M raspi2 -monitor stdio -kernel kernel.elf
