# Ch 12 - Implementing Our Own Interrupts in Real Mode

**Book pages:** 31-33 (Part 4)
**Code updated:** `src/boot.asm`
**Test:** `tests/02-bootloader-prints-hello.sh` (same screen output, different path)

## What we built

The bootloader now registers a software ISR at vector 0x80, then uses it instead of calling `print` directly.

Flow:

1. Set segment registers (`DS = ES = 0x07C0`, `SS = 0`) as before.
2. Compute the IVT entry address: `0x80 * 4 = 0x200`.
3. Write the offset of `print` to physical 0x200 and the segment `0x07C0` to physical 0x202.
4. Load SI with the message pointer.
5. Issue `int 0x80`. The CPU looks up vector 0x80 in the IVT, finds our `(0x07C0:print)` far pointer, and jumps there.

## Two important quirks

### `iret`, not `ret`

When `int 0x80` fires, the CPU pushes `FLAGS`, `CS`, and `IP` onto the stack (3 extra words beyond a normal call). The matching `iret` instruction pops them in reverse and restores `FLAGS`. Using plain `ret` would pop only `IP`, leaving `CS` and `FLAGS` on the stack and resuming at the wrong address.

### Writing the IVT through `SS`

`SS = 0`, so `[ss:0x200]` resolves to physical 0x200, which is exactly the byte that holds vector 0x80's entry. We could have used `[ds:0x200]` instead (since `DS = 0x07C0`, that would point at `0x7E00`, the wrong place), so the choice of segment matters. The book picks `SS` because we know its base is 0.

## What this does not change

Visible output is still "Hello, World!" via BIOS teletype. Only the path from the boot code to the print routine has gained an indirection through the IVT.
