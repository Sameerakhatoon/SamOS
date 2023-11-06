# Lecture 160 - window event functionality for processes part 1

Upstream: PeachOS64BitCourse 408d4ca.

L159 was a no-op for SamOs.

L160 lays the groundwork for getting window events into userland.

## What landed

- `struct window_event_userland` mirrors `struct window_event`
  without the kernel-only keypress slot. The data unions are
  same-sized so the kernel->userland copy can memcpy the whole
  region.
- `window_event_to_userland` copies type + data and panics if
  the data-region sizes ever diverge.
- `vector_grow` reserves N slots and advances the live element
  count without writing values; producers later
  `vector_overwrite`. Used to pre-allocate the per-process
  window-event ring.
- `vector_has` linear-scans for an element by value, returning 0
  on hit. Upstream stores the loop counter into `*index_out`
  regardless of whether a match was found - we preserve that
  behaviour verbatim, including the comment.
- `struct process` gains a `window_events` block (vector +
  index + total_unpopped) and `PROCESS_MAX_WINDOW_EVENTS_RECORDED`
  (1000). `process_init` allocates the vector and pre-grows it
  to the ceiling.

The dequeue path arrives in L161 / L162.

## BIOS test status

Source + link. Build links.
