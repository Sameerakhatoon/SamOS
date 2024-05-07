// Serial console keys into the keyboard layer. See serial_input.h.

#include "serial_input.h"
#include "keyboard.h"
#include "arch/arch.h"
#include "cpu/percpu.h"
#include "cpu/smp.h"

#define SERIAL_INPUT_RING 256

// One producer (the receive handler) and one consumer (the IPI, on the
// same CPU), so the indexes need no lock, only ordering.
static volatile uint8_t serial_ring[SERIAL_INPUT_RING];
static volatile uint32_t serial_head, serial_tail;
static bool serial_last_cr;

static void serial_input_service(void){
    while(serial_head != serial_tail){
        smp_rmb();
        char c = (char)serial_ring[serial_head % SERIAL_INPUT_RING];
        serial_head++;
        // CR LF (a terminal set to send both) is one Enter.
        if(c == '\n' && serial_last_cr){
            serial_last_cr = false;
            continue;
        }
        serial_last_cr = c == '\r';
        if(c == '\n'){
            c = '\r';
        } else if(c == 0x7F){
            c = 0x08;
        }
        keyboard_push(c);
    }
}

void serial_input_init(void){
    smp_input_register(serial_input_service);
}

void serial_input_rx(char c){
    if(serial_tail - serial_head < SERIAL_INPUT_RING){
        serial_ring[serial_tail % SERIAL_INPUT_RING] = (uint8_t)c;
        smp_wmb();
        serial_tail++;
    }
    arch_send_ipi(this_cpu(), ARCH_IPI_INPUT);
}
