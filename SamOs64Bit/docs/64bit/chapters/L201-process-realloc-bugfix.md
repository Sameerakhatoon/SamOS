# Lecture 201 - process_realloc NULL handling fix

Upstream: PeachOS64BitCourse d6af265.

ISO C's `realloc(NULL, n)` is equivalent to `malloc(n)`.
`process_realloc` was missing that case: a NULL `old_virt_ptr`
fell through to `process_allocation_exists` which returned
-ENOTFOUND and userland never got memory.

The fix adds an early-out at the top of `process_realloc`:

    if(old_virt_ptr == NULL){
        return process_malloc(process, new_size);
    }

## BIOS test status

Source + link. Build links.
