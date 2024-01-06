// Lecture 137 - PS/2 mouse driver init path (SOURCE-ONLY).
//
// Wires the PS/2 controller's second port, registers an IRQ12
// callback, runs the standard reset/self-test/enable streaming
// handshake, and binds the per-mouse private struct.
//
// Forward-decl `ps2_mouse_handle_interrupt` because L138 lands
// it. Source-only at L137 since:
//   - the interrupt handler is missing;
//   - mouse.o (its caller of mouse_register) is not yet in
//     FILES.

#include "mouse/ps2mouse.h"
#include "idt/irq.h"
#include "idt/idt.h"
#include "io/io.h"
#include "io/tsc.h"
#include "kernel.h"
#include "status.h"
#include "graphics/graphics.h"   // gfx_lock
#include "graphics/graphics.h"

// L138 will land the body.
void ps2_mouse_handle_interrupt(struct trap_frame* frame);

int ps2_mouse_init(struct mouse* mouse);

struct mouse ps2_mouse = {
    .name = {"ps2_mouse"},
    .init = ps2_mouse_init,
};

struct ps2_mouse ps2_mouse_private = {0};

// Poll the status register for the requested condition.
//
// Upstream slept 100 ms once and then looked a single time,
// so every handshake step cost 100 ms and a slow device read as a
// timeout. Poll every 100 us for up to 100 ms instead.
#define PS2_MOUSE_WAIT_POLLS   1000
#define PS2_MOUSE_WAIT_STEP_US 100
int ps2_mouse_wait(int type){
    for(int i = 0; i < PS2_MOUSE_WAIT_POLLS; i++){
        uint8_t status = insb(PS2_STATUS_PORT);
        if(type == PS2_WAIT_FOR_INPUT_TO_CLEAR){
            if((status & 2) == 0){
                return 0;
            }
        } else if(status & 1){
            return 0;
        }
        udelay(PS2_MOUSE_WAIT_STEP_US);
    }
    return -ETIMEOUT;
}

// A controller write needs the INPUT buffer empty; upstream
// waited for the output buffer to fill instead, i.e. for a byte
// that never arrives before a command is sent.
void ps2_mouse_write(uint8_t byte){
    ps2_mouse_wait(PS2_WAIT_FOR_INPUT_TO_CLEAR);
    outb(PS2_STATUS_PORT, PS2_WRITE_TO_MOUSE);
    ps2_mouse_wait(PS2_WAIT_FOR_INPUT_TO_CLEAR);
    outb(PS2_COMMUNICATION_PORT, byte);
}

// Read one byte from the controller, or -ETIMEOUT.
static int ps2_mouse_read(void){
    if(ps2_mouse_wait(PS2_WAIT_FOR_OUTPUT_TO_BE_SET) < 0){
        return -ETIMEOUT;
    }
    return insb(PS2_COMMUNICATION_PORT);
}

// Lecture 138 - IRQ12 packet handler. Accumulates a 3- or
// 4-byte packet, extracts dx/dy (dy inverted so up is
// positive) plus the left/right/middle button bits, clamps
// the new position against the screen, and forwards the move
// + click into the L132/L133 mouse_* API.
// IRQ12 moves the cursor and windows: graphics lock.
static void ps2_mouse_handle_interrupt_locked(struct trap_frame* frame);

void ps2_mouse_handle_interrupt(struct trap_frame* frame){
    gfx_lock();
    ps2_mouse_handle_interrupt_locked(frame);
    gfx_unlock();
}

static void ps2_mouse_handle_interrupt_locked(struct trap_frame* frame){
    (void)frame;
    static uint8_t packet[4];
    static int     packet_byte_count = 0;
    size_t  ps2_mouse_packet_size = ps2_mouse_private.mouse_packet_size;
    uint8_t data                  = insb(PS2_COMMUNICATION_PORT);

    if(packet_byte_count == 0 && !(data & 0x08)){
        return;
    }

    packet[packet_byte_count++] = data;
    if(packet_byte_count < (int)ps2_mouse_packet_size){
        return;
    }
    packet_byte_count = 0;

    int8_t dx = (int8_t)packet[1];
    int8_t dy = (int8_t)packet[2];
    dy = -dy;
    uint8_t left_button   =  packet[0]       & 0x01;
    uint8_t right_button  = (packet[0] >> 1) & 0x01;
    uint8_t middle_button = (packet[0] >> 2) & 0x01;

    int8_t scroll = 0;
    if(ps2_mouse_packet_size == 4){
        scroll = (int8_t)packet[3];
    }
    if(scroll && left_button && right_button && middle_button){
        // supress warnings.   (sic - upstream typo)
    }

    int x_result = (int)ps2_mouse.coords.x + dx;
    int y_result = (int)ps2_mouse.coords.y + dy;
    struct graphics_info* screen = graphics_screen_info();
    if(x_result < 0) x_result = 0;
    if(y_result < 0) y_result = 0;
    if(x_result > (int)(screen->width  - ps2_mouse.graphic.width)){
        x_result = screen->width  - ps2_mouse.graphic.width;
    }
    if(y_result > (int)(screen->height - ps2_mouse.graphic.height)){
        y_result = screen->height - ps2_mouse.graphic.height;
    }
    mouse_position_set(&ps2_mouse, x_result, y_result);

    MOUSE_CLICK_TYPE click_type = MOUSE_NO_CLICK;
    if(left_button){
        click_type = MOUSE_LEFT_BUTTON_CLICKED;
    }else if(right_button){
        click_type = MOUSE_RIGHT_BUTTON_CLICKED;
    }
    if(click_type != MOUSE_NO_CLICK){
        mouse_click(&ps2_mouse, click_type);
    }
    mouse_moved(&ps2_mouse);
}

int ps2_mouse_init(struct mouse* mouse){
    // Handshake with the aux IRQ bit still clear and only then
    // turn IRQ12 on. Upstream enabled the IRQ first, so once IRQ12 is
    // unmasked the packet handler would eat the ACK / self-test bytes
    // this function waits for. kernel_main calls us
    // with interrupts still off.
    ps2_mouse_wait(PS2_WAIT_FOR_INPUT_TO_CLEAR);
    outb(PS2_STATUS_PORT, PS2_COMMAND_ENABLE_SECOND_PORT);

    // Drop anything stale in the output buffer.
    for(int i = 0; i < 16 && (insb(PS2_STATUS_PORT) & 1); i++){
        insb(PS2_COMMUNICATION_PORT);
    }

    ps2_mouse_write(PS2_MOUSE_RESET);
    if(ps2_mouse_read() != PS2_MOUSE_ACK){
        print("Mouse reset: no ack received\n");
        return -EIO;
    }
    if(ps2_mouse_read() != PS2_MOUSE_SELF_TEST_PASS){
        print("Mouse reset: self-test failed\n");
        return -EIO;
    }
    int device_id = ps2_mouse_read();
    if(device_id < 0){
        device_id = PS2_STANDARD_MOUSE_DEVICE_ID;
    }

    ps2_mouse_write(PS2_MOUSE_ENABLE_PACKET_STREAMING);
    if(ps2_mouse_read() != PS2_MOUSE_ACK){
        print("Mouse enable data reporting: no ack received\n");
        return -EIO;
    }

    mouse->private = &ps2_mouse_private;
    ps2_mouse_private.device_id         = (uint8_t)device_id;
    ps2_mouse_private.mouse_packet_size = 3;
    if(device_id == PS2_SCROLL_WHEEL_MOUSE_DEVICE_ID){
        ps2_mouse_private.mouse_packet_size = 4;
    }

    // Enable the aux clock + IRQ in the controller config byte, then
    // unmask at the PIC.
    ps2_mouse_wait(PS2_WAIT_FOR_INPUT_TO_CLEAR);
    outb(PS2_STATUS_PORT, PS2_READ_CONFIG_BYTE);
    int config_val = ps2_mouse_read();
    if(config_val < 0){
        return -EIO;
    }
    config_val &= ~(1 << 5); // Clear bit 5 to enable the clock.
    config_val |=  (1 << 1); // Set bit 1 to enable interrupts.
    ps2_mouse_wait(PS2_WAIT_FOR_INPUT_TO_CLEAR);
    outb(PS2_STATUS_PORT, PS2_UPDATE_CONFIG_BYTE);
    ps2_mouse_wait(PS2_WAIT_FOR_INPUT_TO_CLEAR);
    outb(PS2_COMMUNICATION_PORT, (uint8_t)config_val);

    idt_register_interrupt_callback(ISR_MOUSE_INTERRUPT, ps2_mouse_handle_interrupt);
    // IRQ_PS2_MOUSE is 12 (the interrupt work fixed the off-by-one enum), and
    // IRQ_enable unmasks the master's cascade line for slave IRQs.
    IRQ_enable(IRQ_PS2_MOUSE);
    return 0;
}

struct mouse* ps2_mouse_get(void){
    return &ps2_mouse;
}
