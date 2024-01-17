#ifndef VIRTIO_INPUT_H
#define VIRTIO_INPUT_H
// virtio-input keyboards as a second key source (virtio_input.c).

// Find every virtio-input keyboard on PCI and start it. Called from
// keyboard_init, after the PS/2 keyboard.
void virtio_input_init(void);

// Linux input key code (EV_KEY) to scancode set 1: the scancode, with
// 0x100 set when it is E0-prefixed; 0 for a key with no set 1 code.
// Exposed for the host test.
int virtio_input_key_to_set1(unsigned int code);

#endif
