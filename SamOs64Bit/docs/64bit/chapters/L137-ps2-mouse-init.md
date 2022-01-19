# Lecture 137 - PS/2 mouse driver init (source-only)

L137 adds `src/mouse/ps2mouse.{h,c}` source-only, plus a new
`ETIMEOUT` errno.

## Port constants

The standard PS/2 8042-controller port map: status / command at
`0x64`, data at `0x60`, the bit-2 / bit-1 ready masks for
input-clear / output-set, the canonical command bytes
(`0xA8` enable second port, `0xD4` write-to-mouse, `0x20`
read-config, `0x60` update-config), the `0xF4` enable-streaming
and `0xFF` reset, plus the `0xFA` ACK and `0xAA` self-test
pass acks. `ISR_MOUSE_INTERRUPT = 0x2C` is the kernel vector
the IRQ12 handler lands on (`L61 PIC remap` offset 0x20 plus
12).

## `struct ps2_mouse`

Per-instance state: `device_id` (which the standard / scroll
ACK reports) and `mouse_packet_size` (3 for standard, 4 for
scroll-wheel).

## `ps2_mouse_wait(type)`

Two modes:

- `PS2_WAIT_FOR_INPUT_TO_CLEAR`: udelay 100 ms, then check
  `status & 0x02 == 0`.
- `PS2_WAIT_FOR_OUTPUT_TO_BE_SET`: udelay 100 ms, then check
  `status & 0x01 == 1`.

Returns `-ETIMEOUT` when the bit is the wrong polarity.

## `ps2_mouse_write(byte)`

Tells the 8042 to write to the mouse (`0xD4`), then writes the
actual byte. Each step is preceded by a wait for the input
buffer to clear.

## `ps2_mouse_init(mouse)`

The standard PS/2 mouse boot dance:

1. `idt_register_interrupt_callback(ISR_MOUSE_INTERRUPT,
   ps2_mouse_handle_interrupt)`. The handler body lands in
   L138; we forward-declare it.
2. `IRQ_enable(IRQ_PS2_MOUSE)` (IRQ12).
3. Send "enable second port" (`0xA8`).
4. Read the config byte, clear bit 5 (enable clock), set bit
   1 (enable interrupts), write it back.
5. `ps2_mouse_write(0xFF)` to reset. Expect ACK + self-test
   pass.
6. Read the device id (standard vs scroll wheel).
7. `ps2_mouse_write(0xF4)` to enable packet streaming. Expect
   ACK.
8. Bind the per-mouse `private` slot to the static
   `ps2_mouse_private`; set packet size by device id.

Upstream defines an unused `out:` label after the body.
Preserved verbatim; `-Wno-unused-label` keeps the build clean.

## `ps2_mouse_get()`

Returns `&ps2_mouse` so `mouse_system_load_static_drivers`
(L132 TODO) can register it.

## Source-only

`ps2_mouse_handle_interrupt` is forward-declared; L138 lands
its body. `ps2mouse.o` is NOT in FILES yet (and won't link
until the handler exists and mouse.o is also in).

## BIOS test status

Source-only. Test asserts both files, every port macro, the
struct, every internal function, the IRQ/IDT register calls,
and that ps2mouse.o is NOT in FILES.
