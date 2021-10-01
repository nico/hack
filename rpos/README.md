raspberry pi os notes
=====================

<https://www.cl.cam.ac.uk/projects/raspberrypi/tutorials/os/screen01.html>

<https://github.com/bztsrc/raspi3-tutorial.git>
<https://github.com/isometimes/rpi4-osdev>

<https://s-matyukevich.github.io/raspberry-pi-os/docs/lesson01/rpi-os.html>

<https://www.cl.cam.ac.uk/projects/raspberrypi/tutorials/os/screen01.html>

<https://github.com/dwelch67/raspberrypi>

<https://jsandler18.github.io/>

<https://wiki.osdev.org/Raspberry_Pi_Bare_Bones>

<https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface>
<https://github.com/raspberrypi/linux/blob/rpi-5.15.y/include/soc/bcm2835/raspberrypi-firmware.h#L36>

<https://www.raspberrypi.org/documentation/computers/processors.html>
<https://www.raspberrypi.org/documentation/hardware/raspberrypi/>
<https://www.raspberrypi.org/documentation/configuration/config-txt/boot.md>

<https://sourceware.org/binutils/docs/ld/Scripts.html#Scripts>

kernel7.img is executed in ARMv7 mode, kernel8.img in AArch64 mode.
64-bit image must be uncompressed.

VideoCore GPU starts first and loads bootcode.bin (in ROM on RPi4).
Reads fixup.dat and start.elf, which reads config.txt (see `boot.md` link
above) and then starts the kernel on the ARM core.

In 64-bit, only one core starts. In 32-bit, 4 cores start at same address,
so need to stop 3 of the 4 cores for now.

`-mcpu=arm1176jzf-s` for kernel.img.
