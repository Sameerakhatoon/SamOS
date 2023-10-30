# Lecture 158 - diverting stdout to a window

Upstream: PeachOS64BitCourse f18967e.

After L157 we can spawn a window. L158 lets the process redirect
its standard output into that window's terminal.

## What landed

- `struct process` gains a `sysout_win` field.
- `process_print_char`, `process_print`, and
  `process_set_sysout_window` are added. The print pair looks up
  the window's terminal via `window_terminal` and feeds the
  character/string through `terminal_write` / `terminal_print`.
- `isr80h_command1_print` and `isr80h_command3_putchar` now call
  `process_print` and `process_print_char` instead of writing
  directly to the kernel console.
- A new `SYSTEM_COMMAND17_SYSOUT_TO_WINDOW`
  (`isr80h_command17_sysout_to_window`) takes a userland window
  handle, resolves it via `process_window_get_from_user_window`,
  and stashes the result on the process.

Upstream calls `process_print` even when `sysout_win` is NULL;
SamOs preserves that behaviour so any divergence is a follow-up
bug commit, not a silent fix. The first call site
(`peachos_divert_stdout_to_window` from `blank.c`) registers the
window before any printf runs, so practically the path is safe.

The upstream commit also touched the userland stdlib
(`programs/stdlib/src/peachos.asm`, `peachos.h`, regenerated
ELFs). SamOs has not ported the userland stdlib yet for these
syscalls; the kernel-side wiring is what matters here. The
userland helpers will follow once the program tree starts using
windows.

## BIOS test status

Source + link. Build links.
