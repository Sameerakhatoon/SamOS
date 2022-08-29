# G02 - Filter key-release and extended scancodes

**Surfaced during:** Ch 45 / G01 (Implementing our PIC code)
**Fix:** in `int21h_handler`, ignore scancodes with the high bit set
**Regression test:** `tests/12-keyboard-fires-repeatedly.sh` (now requires exactly 3 messages for 3 keys, not "at least 3")

## The symptom

After G01 (port 0x60 drain), pressing a key produced TWO "Keyboard pressed!" lines on the screen instead of one. Pressing some other keys (arrows, function keys) produced FOUR.

## The root cause

The PS/2 keyboard sends one scancode for press and another for release. Each one fires IRQ1 separately. So pressing-and-releasing a single key generates two interrupts and two prints.

Extended keys (arrows, modifiers, function keys) send a `0xE0` prefix byte before each scancode. So an arrow key press-and-release sends four bytes total: `0xE0, make, 0xE0, break`. Each byte fires a separate IRQ1. Four prints.

The scancode layout (set 1, which the keyboard controller translates to by default):

| Event | Byte pattern |
|-------|--------------|
| Plain key press | `0x00..0x7F` |
| Plain key release | `(make_code) | 0x80` -> `0x80..0xFF` |
| Extended key press | `0xE0, 0x00..0x7F` |
| Extended key release | `0xE0, 0x80..0xFF` |

Note that:
- Release codes always have the high bit set.
- The `0xE0` prefix byte also has the high bit set.

So a single rule "ignore if high bit set" filters both the release halves AND the `0xE0` prefix, leaving exactly one print per key event.

## The fix

```c
unsigned char sc = insb(0x60);
if((sc & 0x80) == 0){
    print("Keyboard pressed!\n");
}
```

After this change:

- Plain key 'a': `0x1E` (printed), `0x9E` (skipped). 1 print.
- Arrow up: `0xE0` (skipped), `0x48` (printed), `0xE0` (skipped), `0xC8` (skipped). 1 print.

## Verifying the scancodes

Before settling on the fix, a one-shot debug version of the handler printed each scancode as two hex digits. With `sendkey a` the screen showed `1E 9E`, which confirmed set-1 codes and that the high-bit rule was correct.

## Test update

`tests/12-keyboard-fires-repeatedly.sh` previously required "at least 3" messages because pre-G02 each key produced 2 messages (so 3 keys -> 6 messages). With G02 the count is exact: 3 keys -> 3 messages. The test now asserts equality so a future regression in either direction (filter dropped, or filter too aggressive) fails the test.

## What this doesn't fix

We still discard the scancode after the high-bit test. A real keyboard driver needs to translate scancodes into characters, handle modifier state (Shift, Caps Lock, Ctrl), and feed a typed-character queue to whatever wants to read keyboard input. That's many chapters from now.

For now the kernel just confirms "a key was pressed" without caring which.
