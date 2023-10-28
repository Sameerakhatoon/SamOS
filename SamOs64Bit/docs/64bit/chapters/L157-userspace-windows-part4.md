# Lecture 157 - userspace windows part 4

Upstream: PeachOS64BitCourse afb0ecf.

L156 (part three) was a no-op in SamOs because the helpers it
relied on already landed earlier; we skip the empty commit.

L157 brings two changes to the IDT path:

1. `idt_clock` now bails out when `task_current()` is NULL. The
   windowless boot path runs the kernel without a user task, so
   the timer used to NULL-deref inside `task_next`.
2. `interrupt_handler` re-enables `task_current_save_state` and
   `task_page`, both gated on `task_current()`. L140 had
   commented them out wholesale; now they fire only when there
   is a task to switch through.

Upstream also fixed a paging bug here
(`paging_null_entry(pdpt_entries)` -> `paging_null_entry(pdpt_entry)`),
but SamOs's paging port already used the correct variable in
`paging_get`, so nothing to change there.

## BIOS test status

Source + link. Build links.
