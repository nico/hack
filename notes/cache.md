Caching data
============

For caches, it seems useful to tell the OS "here's some data, feel free
to throw it away if you're low on memory. If that happens, I'll check
the next time before I access it, and recompute if needed".

That way, the OS can free cache memory without the involvement of the
process that allocated it, while available memory can be used for caches
without the caching application having to feel bad.

Chromium has `base/memory/discardable_memory*` for this.

In practice, this isn't super well supported.

Note: The rest of this document is fairly unstructured notes, sorry.
Maybe not even 100% accurate, but maybe there are some useful pointers
in there.

android
-------

Android's ashmem used to have fairly explicit support for this, but it got
removed from linux in favor of memfd, which doesn't have the right semantics.

linux
-----

`MADV_COLD` is a thing:

<https://kernelnewbies.org/Linux_5.4#Two_new_madvise.28.29_flags:_MADV_COLD_and_MADV_PAGEOUT>

https://nullprogram.com/blog/2019/12/29/

https://github.com/skeeto/purgeable

looks similar to what `base/memory/madv_free_discardable_memory_posix.cc` does,
maybe?

It looks fairly janky API-wise.

mac
---

macOS has APIs that support this directly, which is nice!

But anecdotally, If you use them, you'd be asked to purge your data fairly
soon on most machines (< 1 min).

<https://developer.apple.com/library/archive/documentation/Performance/Conceptual/ManagingMemory/Articles/CachingandPurgeableMemory.html>

```
NSPurgeableData * data = [[NSPurgeableData alloc] init];
[data endContentAccess]; //Don't necessarily need data right now, so mark as discardable.
//Maybe put data into a cache if you anticipate you will need it later.
 
...
 
if([data beginContentAccess]) { //YES if data has not been discarded and counter variable has been incremented
     ...Some operation on the data...
     [data endContentAccess] //done with the data, so mark as discardable
} else {
     //data has been discarded, so recreate data and complete operation
     data = ...
     [data endContentAccess]; //done with data
}
 
//data is able to be discarded at this point if memory is tight
```


does this internally do what `base/memory/discardable_shared_memory.cc` does
w `MADV_FREE_REUSABLE`?

no: NSPurgeableData beginContentDaddress calls this from CoreFoundation:

```
kern_return_t _CFDiscorporateMemoryMaterialize(CFDiscorporateMemory *hm) {
    kern_return_t ret = KERN_SUCCESS;
    if (hm->corporeal) ret = KERN_INVALID_MEMORY_CONTROL;
    if (KERN_SUCCESS == ret) ret = mach_vm_map(mach_task_self(), &hm->address, hm->size, 0, VM_FLAGS_ANYWHERE, hm->port, 0, FALSE, VM_PROT_DEFAULT, VM_PROT_DEFAULT, VM_INHERIT_DEFAULT);
    if (KERN_SUCCESS == ret) hm->corporeal = true;
    int state = VM_PURGABLE_NONVOLATILE;
    if (KERN_SUCCESS == ret) ret = vm_purgable_control(mach_task_self(), (vm_address_t)hm->address, VM_PURGABLE_SET_STATE, &state);
    if (KERN_SUCCESS == ret) if (VM_PURGABLE_EMPTY == state) ret = KERN_PROTECTION_FAILURE; // same as VM_PURGABLE_EMPTY
    return ret;
}
```

https://opensource.apple.com/source/CF/CF-744.12/CFUtilities.c.auto.html

`mmap` `VM_FLAGS_PURGABLE` in `_CFDiscorporateMemoryAllocate` there (also
mentioned in `man mmap`).

`IOSurfaceSetPurgeable`

win
---

XXX

serenityos
----------

Uses the macOS design, `MAP_PURGEABLE` mmap flag:
<https://github.com/SerenityOS/serenity/commit/2d1a651e0aa8be1836a07af2bbae5dfbe522de09>

(Mostly interesting because written by someone who used to be at Apple, and
for my notes.)

--------

`MADV_PURGE_ARGUMENT` is just a define in base
