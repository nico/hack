; nicertiny64.asm for Mac OS X (Mach-O Object File Format)
; nasm -f bin -o nicertiny64 nicertiny64.asm

; Based on http://osxbook.com/blog/2009/03/15/crafting-a-tiny-mach-o-executable/
; Modified to produce a 64-bit binary.

; dq requires a nasm newer than the OS X default, so use 2 dd per dq

        org   0x1000

        db    0xcf, 0xfa, 0xed, 0xfe       ; magic
        dd    0x1000007                    ; cputype (CPU_TYPE_X86_64)
        dd    3                            ; cpusubtype (CPU_SUBTYPE_X86_64_ALL)
        dd    2                            ; filetype (MH_EXECUTE)
        dd    3                            ; ncmds
        dd    _start - _cmds               ; cmdsize
        dd    0                            ; flags
        dd    0                            ; reserved
_cmds:
        ; 64-bit binaries seem to need a protected zero page to run.
        dd    0x19                         ; cmd (LC_SEGMENT64)
        dd    0x48                         ; cmdsize
        db    "__PAGEZERO"                 ; segname
        db    0, 0, 0, 0, 0, 0             ; segname padding
        dd    0x0000, 0                    ; vmaddr
        dd    0x1000, 0                    ; vmsize
        dd    0, 0                         ; fileoff
        dd    0, 0                         ; filesize
        dd    0                            ; maxprot
        dd    0                            ; initprot
        dd    0                            ; nsects
        dd    0                            ; flags

        dd    0x19                         ; cmd (LC_SEGMENT64)
        dd    0x98                         ; cmdsize
        db    "__TEXT"                     ; segname
        db    0, 0, 0, 0, 0, 0, 0, 0, 0, 0 ; segname padding
        dd    0x1000, 0                    ; vmaddr
        dd    0x1000, 0                    ; vmsize
        dd    0, 0                         ; fileoff
        dd    filesize, 0                  ; filesize
        dd    7                            ; maxprot
        dd    5                            ; initprot
        dd    1                            ; nsects
        dd    0                            ; flags
        db    "__text"                     ; sectname
        db    0, 0, 0, 0, 0, 0, 0, 0, 0, 0 ; sectname
        db    "__TEXT"                     ; segname
        db    0, 0, 0, 0, 0, 0, 0, 0, 0, 0 ; segname
        dd    _start, 0                    ; addr
        dd    _end - _start, 0             ; size
        dd    _start - 0x1000, 0           ; offset
        dd    2                            ; align
        dd    0                            ; reloff
        dd    0                            ; nreloc
        dd    0                            ; flags
        dd    0                            ; reserved1
        dd    0                            ; reserved2

        ; /usr/include/mach/i386/thread_status.h
        dd    5                            ; cmd (LC_UNIXTHREAD)
        dd    0xb8                         ; cmdsize
        dd    4                            ; flavor (x86_THREAD_STATE64)
        dd    0x2a                         ; count (x86_THREAD_STATE64_COUNT)
        ;     rax    rbx    rcx    rdx    rdi     rsi    rbp    rsp
        dd    0, 0,  0, 0,  0, 0,  0, 0,  42, 0,  0, 0,  0, 0,  0, 0
        ;     r8     r9     r10    r11    r12    r13    r14    r15
        dd    0, 0,  0, 0,  0, 0,  0, 0,  0, 0,  0, 0,  0, 0,  0, 0
        ;     rip         rflag  cs     fs     gs
        dd    _start, 0,  0, 0,  0, 0,  0, 0,  0, 0
_start:

        ; OS X's default nasm can't do 64-bit asm, so insert machine code.
        db 0x48, 0xc7, 0xc0, 01, 00, 00, 02 ; mov rax, 0x2000001  ; SYSCALL_EXIT
        db 0x0f, 0x05  ; syscall

_end:
filesize equ  $ - $$
