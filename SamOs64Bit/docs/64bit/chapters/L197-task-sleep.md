# Lecture 197 - tasks can sleep

Upstream: PeachOS64BitCourse a7ae9ee.

## What landed

- `struct task` gains a `sleeping.sleep_until_microseconds`
  TSC deadline.
- `task_asleep`: deadline > tsc_microseconds().
- `task_sleep`: set the deadline.
- `task_get_next_non_sleeping_task`: rotate from current
  forward skipping sleepers, wrap at task_head, return -EIO if
  the ring is empty.
- `task_next` now loops over non-sleeping tasks. If the whole
  ring is asleep it `udelay(100)`'s and retries instead of
  panicking.

## Upstream bug preserved

`task_sleep` sets the deadline to `tsc_microseconds() *
microseconds` (multiplication). The obvious intent is
`+ microseconds`. As written almost any caller will sleep for
years; preserved verbatim per the project rule, with a code
comment explaining the bug.

## BIOS test status

Source + link. Build links.
