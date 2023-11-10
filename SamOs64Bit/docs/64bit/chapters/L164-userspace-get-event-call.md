# Lecture 164 - calling isr80h get-event from userspace

Upstream: PeachOS64BitCourse d1ce37e.

L164 is mostly userland: `blank.c` swaps its fopen/fstat test
for a `while(1) peachos_process_get_window_event(...)` loop, and
`programs/stdlib` gains the `peachos_process_get_window_event`
helper plus a userland `struct window_event` mirror.

## SamOs kernel-side delta

The kernel changes in this lecture are:

- `isr80h/window.c` returns `(void*)(uintptr_t)res` instead of
  the raw int. SamOs already cast through `intptr_t` when L163
  landed, so nothing to do here.
- `process.c` switches `vector_Free` (capital F, an upstream
  typo introduced in L161) back to `vector_free`. SamOs never
  shipped the typo (we noted it during L161 and used the correct
  name), so this is already in place.
- `process.h` and `process.c` get a forward declaration of
  `struct window_event` and `process_window_closed`. SamOs added
  both during L160 and L162; nothing to do.

The userland stdlib + blank.c diff is intentionally not ported:
SamOs's program tree does not track upstream's stdlib commits.
A later batch will revisit userland wholesale.

## Test

`tests64/L164-userspace-get-event-call.sh` re-asserts that all
the kernel-side L164 invariants are in place after the prior
ports (correct `vector_free`, forward decls present,
`(intptr_t)res` cast).

## BIOS test status

Source + link. Build links.
