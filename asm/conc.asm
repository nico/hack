// Based on
// http://davidad.github.io/blog/2014/03/23/concurrency-primitives-in-intel-64-assembly/

#define SYSCALL_EXIT      0x2000001
#define SYSCALL_FORK      0x2000002
#define SYSCALL_WRITE     0x2000004
#define SYSCALL_OPEN      0x2000005
#define SYSCALL_MMAP      0x20000C5
#define SYSCALL_FTRUNCATE 0x20000C9
// http://www.opensource.apple.com/source/xnu/xnu-1504.3.12/bsd/kern/syscalls.master

#define O_RDWR        0x0002
#define O_CREAT       0x0200

#define MAP_SHARED    1
#define MAP_ANON      0x1000

#define PROT_READ     1
#define PROT_WRITE    2

.intel_syntax

.global start
start:
  mov r14, 2*8  # File size   (XXX: set via define)
  mov r15, 4    # Num workers (XXX: set via define)

  cmp qword ptr [rsp], 1  # Exit unless command-line arguments are present.
  je map_anon

open_file:
  mov rdi, [rsp + 2*8]  # 0: argc, 1: argv[0], 2: argv[1]
  mov rsi, O_RDWR | O_CREAT
  mov rdx, 0660
  mov rax, SYSCALL_OPEN
  syscall
  cmp rax, -1
  je exit1
  mov r13, rax  # file descriptor returned by syscall

  mov rdi, r13
  mov rsi, r14
  mov rax, SYSCALL_FTRUNCATE

  mov r8, r13
  mov r10, MAP_SHARED
  jmp mmap


map_anon:
  mov r10, MAP_SHARED | MAP_ANON   # MAP_ANON means not backed by a file
  mov r8,  -1

mmap:  # r8: fd, r10: mmap flags
  mov rdi, 0                       # addr
  mov rsi, r14                     # len
  mov rdx, PROT_READ | PROT_WRITE  # prot
                                   # flags already in r10
                                   # fd already in r8
  mov r9, 0                        # pos
  mov rax, SYSCALL_MMAP
  syscall
  cmp rax, -1
  je exit1
  mov rbp, rax  # The mapped memory!


fork:
  mov rdi, 1
  lea rsi, qword ptr [rip+forkstr]
  mov rdx, [rip+forkstrlen]
  mov rax, SYSCALL_WRITE
  syscall

  mov eax, SYSCALL_FORK
  syscall
  # http://www.opensource.apple.com/source/xnu/xnu-2050.18.24/libsyscall/custom/__fork.s
  cmp rdx, 0  # rdx 0 => parent, 1 => child
  jne child
  dec r15
  jnz fork

  mov rdi, 42
  mov rax, SYSCALL_EXIT
  syscall

exit1:
  mov rdi, 1
  mov rax, SYSCALL_EXIT
  syscall

child:
  mov rdi, 1
  lea rsi, qword ptr [rip+childstr]
  mov rdx, [rip+childstrlen]
  mov rax, SYSCALL_WRITE
  syscall
  jmp exit1

forkstr:    .ascii "fork\n"
forkstrlen: .quad . - forkstr
childstr:    .ascii "child\n"
childstrlen: .quad . - childstr
