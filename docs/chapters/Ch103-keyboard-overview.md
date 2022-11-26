# Ch 103 - Understanding keyboard access in protected mode (book Ch 109)

Book Ch 109: how the keyboard layer will be structured. Prose only;
no code changes.

## Design

- One **keyboard driver** per supported keyboard family (PS/2 today,
  USB HID later if we get that far). Drivers chain into a linked list
  on `struct keyboard { init; name[20]; next; }`. The kernel registers
  PS/2 at boot; user-land never sees this layer directly.
- One **keyboard buffer** per **process**, not one global pool. A
  ring buffer in `struct process` (or hung off it) with `head`/`tail`
  indices and `head % SIZE`/`tail % SIZE` wrap-around. Keystrokes
  destined for the currently-running process go into its own buffer,
  so background processes don't steal keys from the foreground one.
- IRQ1 (vector 0x21) still fires on every PS/2 scancode like it has
  since Ch 45. The handler just stops calling `print` and starts
  pushing the resolved character into `current_process->keyboard`.
- A new syscall (Ch 113 or so) reads one character out of the current
  process's buffer; user-land's `getkey()` is just a wrapper around
  that syscall.

## Why per-process, not global

If two processes both want keys, a global buffer means whichever calls
`getkey` first wins and the other never sees the keystroke. Per-
process buffers turn keyboard input into a stream the kernel routes
to the active task, like every modern OS does.

## What's next

- Ch 110 (book): `struct keyboard`, `struct keyboard_buffer`,
  `keyboard_init` chain, `keyboard_backspace`, `keyboard_push`,
  `keyboard_pop`, registration into `struct process`.
- Ch 111+: classic PS/2 driver registered with the chain; replace the
  Ch 45 `print("Keyboard pressed!")` debug line with a real
  `keyboard_push`.
- Eventually: `getkey()` syscall + a user-space program that reads
  characters and prints them back, proving the full path.
