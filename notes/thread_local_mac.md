# Thread-local storage implementation details on macOS

I read up on how thread-locals are implemented on macOS recently. Here's a
write-up. There's decent documentation about how this works on Linux, but I
haven't found anything that describes macOS. The following is all
macOS-specific, it's based on reading dyld source (dyld is macOS's dynamic
linker).

In C++, you can declare a global variable, like so (we're
only looking at variables and ignoring constants):

    int a = 4;
    int b = 0;

The loader (technically, the dynamic linker, dyld) arranges things so that all
references to `a` point to memory that stores the number "4" at startup, and
all references to `b` point to memory that stores "0".

This is done by storing "4" somewhere in the executable and
mmap()ing that memory to where "a" is stored. Zero-initialized
memory is special in that it doesn't need to be stored in the
executable -- the loader maps `b`'s address to zero-initialized
(as opposed to file-backed) memory.

That's why `int a[10000] = {};` (10000 ints that are all 0)
produces a .o file that's 400 bytes large (the zeros don't
have to be stored), while `int a[10000] = {1};` (a single int
that's 1 followed by 9999 ints that are zero) produces an
object file that's 40 kiB -- not all of the initial data is 0,
so it needs to be stored in the .o file. At runtime, both
arrays need the same virtual memory size, but the zero-initialized data can be
mapped to a copy-on-write zero page so it'll only need actual physical memory
when parts of it are filled with non-zeros at runtime. So it's not just a disk
space optimization.

All global variables end up in a single big chunk of memory
(well, two: One for zero-initialized globals, and one for
non-zero variables). If you have a global `int a = 4;` in one cc file and
`int b = 5;` in another, it's possible that they'll end up right next to each
other in memory.

You can add `thread_local` to the variable to make them
thread-local. This means every thread has its own copy of these
variables:

    thread_local int a = 4;
    thread_local int b = 0;

Conceptually, this is similar to globals -- except the loader
(technically, the dynamic linker dyld) conceptually now has to initialize these
variables every time a thread is started.

I say "conceptually" because this is lazily done at the time a thread refers to
any thread\_local for the first time. When that happens, the thread checks if
its thread-local memory has been initialized, and if not, it creates a region
of memory just for that particular thread that has enough room for all
thread-local variables in the program, and copies the right initial values for
all thread-local variables into that region.

The way this works is that the executable stores the initial values of all
thread-local variables in a memory area (namely, the `__DATA,__thread_data`
section, immediately followed by the `__DATA,__thread_bss` section -- the former
stores all the non-zero thread-local variables, while
the second just stores the size of all zero-initalized thread-local variables).

`__DATA,__thread_data` stores initialization data. `__DATA,__thread_bss`
doesn't need to store anything (it's used to initialize variables to 0),
but it needs to point somewhere valid in the file, so it points at
file offset 0.

The executable also stores metadata for each thread-local variable in the
`__DATA,__thread_vars` section. This stores, for every thread-local function, a
3-tuple consisting of:

* a function returning the address of the thread-local variable for the current
  thread
* a pointer to the per-thread thread-local variable data storage block
  (initially nullptr when TLS isn't initialized for the current thread yet)
* the offset of this variable in that block

`__DATA,__thread_vars` has type `S_THREAD_LOCAL_VARIABLES` (see `otool -lv`
output). dyld identifies TLV descriptors based on this section type.

When you reference a thread-local variable in your code, this gets compiled to
a call to the function returning the variable's address. The dynamic linker
sets this function to `tlv_get_addr` (a dyld-internal function) for every
thread-local variable.

Let's say we read `a`. This gets compiled to:

	movq	_i@TLVP(%rip), %rdi
	callq	*(%rdi)
	movl	(%rax), %r14d

`_i@TLVP(%rip)` refers to that 3-tuple mentioned above, and the first element
of it is set to `tlv_get_addr` (and the 2nd element is a pointer to the
thread's TLS memory region, and the third is the offset), which gets called
(which means every TLS access is an indirect call, which is bad for the branch
predictor.) tlv\_get\_addr does the following (only the happy fast path is
shown):

    movq	8(%rdi),%rax
    movq	%gs:0x0(,%rax,8),%rax
    testq	%rax,%rax
    je		LlazyAllocate
    addq	16(%rdi),%rax
    ret

The `8(%rdi)` reads the 2nd element of the tuple, which (after a layer of
indirection via %gs that we'll take for given for now) gets the pointer to the
current thread's TLS storage region. If it's not yet initialized, we jump
somewhere that does TLS setup for the current thread. If it is initialized, we
return the pointer at the variable's offset.

That's then dereferenced by the last of the three lines above
(`movl (%rax), %r14d`).

This design makes taking the address of a thread-local variable trivial: just
don't dereference the returned pointer.

What if you have `thread_local int a = getpid();`? When is `getpid()` called?

This is compiled similarly to a function-local static: The compiler generates
a thread-local guard variable, checks if it's set every time `a` is accessed,
and if not, sets it and sets a to the result of `getpid()`. That way, the
dynamic linker doesn't have to be able to call code to initialize thread-locals,
the machinery described above is sufficient.

(dyld does have a feature to call an array of per-thread function
pointers after it sets up the per-thread data section. However, clang doesn't
use it and goes with the TLS guard variables instead. I'm guessing that's so
that each thread-local that's initialized with a call can be initialized lazily
on first access, and in access order, instead of running all thread-local
initializers eagerly on TLS initialization.)

What if you say `extern thread_local int a;` and refer to that? That generates
a call to a wrapper function that behind the scenes does the indirection via
the TLVP described above. That's why if you look at disassembly output for
`thread_local int a;` you end up with a definition for that wrapper function,
lovingly called `__ZTW1a` -- some other TU might call it. If you say
`static thread_local int a;` (or use -fvisibility=hidden), you don't get
that wrapper -- just like with normal inline functions.

Here's an example program you can play with:

    #include <stdio.h>
    #include <unistd.h>

    thread_local int i = 0;
    thread_local int j = 4;
    thread_local int k[40] = {};
    thread_local int z = getpid();

    int main() {
      printf("%d %d %d %d\n", i, j, k[0], z);
      return 0;
    }

Compile and link this (`clang -c b.cc`, `clang b.o`) and look at:

    % dsymutil -s b.o  # Prints symbol table
    % otool -r b.o  # Prints relocations, refers to symbol entries
    % otool -s __DATA __thread_vars b.o  # Prints the 3-tuple area

to get the .o view. In the output, `_i` refers to the descriptor (the 3-tuple),
`_i$tlv$init` refers to the initial value (for `i`, it's 0, so it's in the
zero-initialized `__DATA,__thread_bss` section), and `__ZTW1i` is the wrapper
that other TUs that access `i` via `extern thread_local int i;` would call.

Look at

    % otool -s __DATA __thread_vars a.out
    % otool -s __DATA __thread_data a.out
    % otool -lv a.out | grep -A10 __thread
    % ~/src/llvm-project/out/gn/bin/llvm-objdump --macho --bind a.out
    % otool -tV a.out

to get the executable view.

Note how `__thread_bss` has an `addr` in `otool -l` output right after
`__thread_data`.

(`__tlv_bootstrap` is replaced with `tlv_get_addr` by dyld at load time.
If you run `otool -s __DATA __thread_bss a.out`, remember that `__thread_bss`
points at the start of the file instead of at actual initialization data
since it's used for zero-initialized memory. But when dyld maps the executable,
it'll map the tail of segments that have a bigger memory size than file size
as `VM_PROT_ZF`, so the offset doesn't matter. That's also why zerofill sections
are always at the end of their segment.)

Exercise: Change the thread-locals to either `static thread_local....` or
`extern thread_local` -- in the latter case, don't forget to delete the
initializers -- and see how the output changes.

