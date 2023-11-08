# Lecture 161 - window event functionality for processes part 2

Upstream: PeachOS64BitCourse 39458ed.

## What landed

- `process_push_window_event` writes the event into
  `vector[index % MAX]` and bumps `total_unpopped`. The kernel
  window pointer in the event is nulled before storing so the
  copied event cannot outlive its mapping.
- `process_pop_window_event` reads `vector[0]` (yes, slot 0, not
  a sliding head) and decrements `total_unpopped`. Returns
  -EINVARG on bad args, -EOUTOFRANGE on empty, 0 on success.
- `process_free_process` now drops the window-events vector.

## Upstream typo

Upstream wrote `vector_Free(process->window_events.vector)`
(capital F), which would not compile. We use the correct
`vector_free` and leave a code comment marking the upstream typo
so the audit trail records why our copy diverges.

## Ring bug noted

Producer writes at `index % MAX` (with `index` never
incremented) and the consumer always reads slot 0. As written
the queue holds at most one slot of meaningful data; multiple
producers stomp it before any consumer drains. We preserve the
upstream behaviour verbatim so any fix lands as its own commit
with a clear "fix" tag, not silently mixed into this port.

## BIOS test status

Source + link. Build links.
