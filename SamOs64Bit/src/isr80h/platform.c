// Time, shutdown and reboot syscalls. No arguments;
// register convention as every syscall since the scheduler work.
//
// There are no users or permissions yet, so any process may power
// the machine off, as the shell's builtins do.

#include "platform.h"
#include "acpi/acpi.h"
#include "io/rtc.h"

// long time(void): seconds since 1970-01-01 UTC from the CMOS RTC,
// or -1 when the clock cannot be read.
void* isr80h_command48_time(struct trap_frame* frame){
    return (void*)rtc_unix_time();
}

void* isr80h_command49_shutdown(struct trap_frame* frame){
    acpi_shutdown();
}

void* isr80h_command50_reboot(struct trap_frame* frame){
    acpi_reboot();
}
