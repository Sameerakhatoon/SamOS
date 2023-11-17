#ifndef ISR80H_H
#define ISR80H_H

enum SystemCommands {
    SYSTEM_COMMAND0_SUM,
    SYSTEM_COMMAND1_PRINT,
    SYSTEM_COMMAND2_GETKEY,
    SYSTEM_COMMAND3_PUTCHAR,
    SYSTEM_COMMAND4_MALLOC,
    SYSTEM_COMMAND5_FREE,
    SYSTEM_COMMAND6_PROCESS_LOAD_START,
    SYSTEM_COMMAND7_INVOKE_SYSTEM_COMMAND,
    SYSTEM_COMMAND8_GET_PROGRAM_ARGUMENTS,
    SYSTEM_COMMAND9_EXIT,
    SYSTEM_COMMAND10_FOPEN,  // L105 - userland fopen
    SYSTEM_COMMAND11_FCLOSE, // L106 - userland fclose
    SYSTEM_COMMAND12_FREAD,  // L107 - userland fread
    SYSTEM_COMMAND13_FSEEK,  // L111 - userland fseek
    SYSTEM_COMMAND14_FSTAT,  // L112 - userland fstat
    SYSTEM_COMMAND15_REALLOC, // L115 - userland realloc
    SYSTEM_COMMAND16_WINDOW_CREATE, // L154 - userland window create
    SYSTEM_COMMAND17_SYSOUT_TO_WINDOW, // L158 - divert stdout to a window
    SYSTEM_COMMAND18_GET_WINDOW_EVENT, // L163 - drain a window event
    SYSTEM_COMMAND19_WINDOW_GRAPHICS_GET, // L165 - userspace graphics info
    SYSTEM_COMMAND20_GRAPHICS_PIXELS_BUFFER_GET, // L166 - userspace pixels mapping
    SYSTEM_COMMAND21_WINDOW_REDRAW, // L167 - userspace window redraw
    SYSTEM_COMMAND22_GRAPHICS_CREATE, // L171 - userspace relative graphics create
};

void isr80h_register_commands();

#endif
