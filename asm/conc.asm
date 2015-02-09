// Based on
// http://davidad.github.io/blog/2014/03/23/concurrency-primitives-in-intel-64-assembly/

#define SYSCALL_EXIT      0x2000001
#define SYSCALL_WRITE     0x2000004

.intel_syntax

.global start
start:
  mov rdi, 1
  lea rsi, qword ptr [rip+str]  # [rip+str@GOTPCREL] for GOT instead of rip-rel
  mov rdx, [rip+len]  # len instead of mylen because llvm.org/PR22511
  mov rax, SYSCALL_WRITE
  syscall

  mov rdi, 42
  mov rax, SYSCALL_EXIT
  syscall

str:
  .ascii "Hello, ASM.\n"
  .set mylen, .-str  # See llvm.org/22511
len: .quad . - str
