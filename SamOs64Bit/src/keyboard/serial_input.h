#ifndef KEYBOARD_SERIAL_INPUT_H
#define KEYBOARD_SERIAL_INPUT_H
// Keys typed on the serial console. A board without a PS/2
// keyboard (riscv64 virt) still has its UART: the arch's receive
// interrupt hands each byte to serial_input_rx, and the bytes reach
// the focused process through keyboard_push, as PS/2 and virtio-input
// keys do. A byte is already a character, so there is no scancode
// translation: Enter (CR, or LF from a pipe) becomes CR as the PS/2
// table gives it, DEL becomes backspace (0x08); escape sequences
// (arrows from a terminal) pass through as classic.c produces them.
//
// The receive handler may run inside a disk wait (the UART is just
// another device line), where keyboard_push's locks must not be
// taken, so it only queues the byte and sends ARCH_IPI_INPUT to its
// own CPU, as virtio_input.c does.

// Register the input work (smp_input_register). Once, before the
// arch enables the receive interrupt.
void serial_input_init(void);
// From the UART's receive interrupt handler.
void serial_input_rx(char c);

#endif
