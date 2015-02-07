#define SYSCALL_EXIT      0x2000001

.intel_syntax

.global start
start:
  mov rdi, 42
  mov rax, SYSCALL_EXIT
  syscall
