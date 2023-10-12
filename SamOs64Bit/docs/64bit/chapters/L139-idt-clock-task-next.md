# Lecture 139 - idt_clock prints and calls task_next

L139 (PS/2 mouse part three) is a tiny correction in
upstream: it uncomments two lines inside `idt_clock` so the
timer IRQ both:

- writes a "test" line every tick (debug breadcrumb), and
- calls `task_next()` to round-robin to the next task.

These were commented out earlier during the rewrite.

## SamOs status

We already had both lines live in `idt_clock` from prior
sweeps. This commit is a stay-in-sync notification - no
source change; the test asserts both calls are present so any
future regression that re-comments them gets caught.

## BIOS test status

Source-only. Test extracts the `idt_clock` body and confirms
the EOI write, the `print("test...")`, and the `task_next()`
call all live. Build links.
