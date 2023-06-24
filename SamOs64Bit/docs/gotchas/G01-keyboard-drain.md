# G01 - Keyboard handler must drain port 0x60

**Surfaced during:** Ch 45 (Implementing our PIC code)
**Fix:** one `insb(0x60)` in `int21h_handler` before the EOI
**Regression test:** `tests/12-keyboard-fires-repeatedly.sh`

## The symptom

After Ch 45 shipped, pressing one key produced "Keyboard pressed!" on screen. Pressing more keys produced nothing. The kernel did not crash and did not freeze, it just stopped seeing keypresses.

## The root cause

The PS/2 keyboard controller works like this:

1. Key pressed.
2. Controller stores the scancode in its 1-byte output buffer at port 0x60.
3. Controller raises IRQ1 to tell the host "I have a byte for you".
4. Host's interrupt handler reads port 0x60. This both takes the byte AND clears the "output buffer full" flag.
5. Controller is now ready to deliver the next scancode and will raise IRQ1 again when the next key comes in.

The book's Ch 45 handler skips step 4:

```c
void int21h_handler(){
    print("Keyboard pressed!\n");
    outb(0x20, 0x20);    // EOI to PIC
}
```

So the controller's output buffer stays full forever. The host has cleared the PIC's "in service" flag (via EOI to port 0x20), so as far as the PIC is concerned things are fine. But the controller never gets to raise IRQ1 again because its buffer is still full from the first key.

Result: exactly one keypress message per boot.

## The fix

Add a single `insb(0x60)` before the EOI:

```c
void int21h_handler(){
    print("Keyboard pressed!\n");
    (void)insb(0x60);    // drain the keyboard data port
    outb(0x20, 0x20);    // EOI to PIC
}
```

We discard the scancode (we are not interpreting it yet, that comes in a later keyboard chapter). The point is to drain the controller's output buffer so it can raise IRQ1 for subsequent keys.

`(void)` makes it explicit that we are throwing away the return value, suppresses any "unused result" warning.

## Why the book leaves this out

The book's Ch 45 explicitly admits the handler is a stub for IDT wiring demonstration. The full keyboard driver comes in a later chapter (around Ch 130 in the full keyboard arc). The "fix" here is really just bringing forward one line from that later chapter so the Ch 45 demo behaves the way a reader expects.

We follow the SamOs convention: book-verbatim in Ch 45's commit, then this gotcha-fix commit on top with a docs/gotchas note.

## Regression test

`tests/12-keyboard-fires-repeatedly.sh` boots the kernel, sends three keys via QEMU monitor (`sendkey a`, `sendkey b`, `sendkey c`), then counts "Keyboard pressed!" lines in the VGA buffer. Requires at least 3.

Pre-G01: would fail with count = 1.
Post-G01: passes.

## Related

The Ch 45 chapter note's "Quirks worth noting" section already called this out:

> We do not drain port 0x60. ... A real keyboard driver (later chapter) will do `insb(0x60)` to drain.

G01 just makes that one-line fix now so demos behave correctly.
