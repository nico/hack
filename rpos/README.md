raspberry pi os notes
=====================


<https://wiki.osdev.org/Raspberry_Pi_Bare_Bones> !
<https://www.cl.cam.ac.uk/projects/raspberrypi/tutorials/os/screen01.html>

<https://github.com/raspberrypi/documentation/tree/JamesH65-mailbox_docs/configuration/mailboxes>



<https://www.raspberrypi.org/documentation/configuration/config-txt/boot.md>

<https://sourceware.org/binutils/docs/ld/Scripts.html#Scripts>


kernel7.img is executed in ARMv7 mode, kernel8.ig in AArch64 mode.
64-bit image must be uncompressed.

VideoCore GPU starts first and loads bootcode.bin (in ROM on RPi4).
Reads fixup.dat and start.elf, which reads config.txt (see `boot.md` link
above) and then starts the kernel on the ARM core.

In 64-bit, only one core starts. In 32-bit, 4 cores start at same address,
so need to stop 3 of the 4 cores for now.

`-mcpu=arm1176jzf-s` for kernel.img.
