#include "isr80h.h"
#include "config.h"
#include "kernel.h"
#include "task/task.h"
#include "graphics/graphics.h"
#include "misc.h"
#include "io.h"
#include "heap.h"
#include "process.h"
#include "file.h" // L105 - command 10 fopen
#include "window.h" // L154 - command 16 window_create
#include "graphics.h" // L166 - command 20 graphics pixels
#include "time.h"     // L198 - command 25 udelay
#include "platform.h" // Commands 48-50
#include "net.h"      // Commands 60-61

static ISR80H_COMMAND isr80h_commands[SAMOS_MAX_ISR80H_COMMANDS];

void isr80h_register_command(int command_id, ISR80H_COMMAND command){
    if(command_id < 0 || command_id >= SAMOS_MAX_ISR80H_COMMANDS){
        panic("The command is out of bounds\n");
    }
    if(isr80h_commands[command_id]){
        panic("Your attempting to overwrite an existing command\n");
    }
    isr80h_commands[command_id] = command;
}

static void* isr80h_handle_command(int command, struct trap_frame* frame){
    void* result = 0;
    if(command < 0 || command >= SAMOS_MAX_ISR80H_COMMANDS){
        return 0;
    }
    ISR80H_COMMAND command_func = isr80h_commands[command];
    if(!command_func){
        return 0;
    }
    // The window and graphics syscalls run under the
    // graphics lock as a whole; none of them blocks.
    bool gfx = command >= SYSTEM_COMMAND16_WINDOW_CREATE &&
               command <= SYSTEM_COMMAND24_UPDATE_WINDOW;
    if(gfx){
        gfx_lock();
    }
    result = command_func(frame);
    if(gfx){
        gfx_unlock();
    }
    return result;
}

// Lecture 52 - syscall entry: kernel_page in, dispatch, task_page
// out. The command comes from the frame and the result goes back
// through it instead of a global slot. Moved here from the x86
// trap code.
void isr80h_handler(struct trap_frame* frame){
    kernel_page();
    task_current_save_state(frame);
    task_check_killed();
    int command = (int)trap_frame_syscall_nr(frame);
    trap_frame_set_ret(frame, (uint64_t)isr80h_handle_command(command, frame));
    task_page();
}

void isr80h_register_commands(){
    isr80h_register_command(SYSTEM_COMMAND0_SUM,    isr80h_command0_sum);
    isr80h_register_command(SYSTEM_COMMAND1_PRINT,  isr80h_command1_print);
    isr80h_register_command(SYSTEM_COMMAND2_GETKEY,  isr80h_command2_getkey);
    isr80h_register_command(SYSTEM_COMMAND3_PUTCHAR, isr80h_command3_putchar);
    isr80h_register_command(SYSTEM_COMMAND4_MALLOC,  isr80h_command4_malloc);
    isr80h_register_command(SYSTEM_COMMAND5_FREE,    isr80h_command5_free);
    isr80h_register_command(SYSTEM_COMMAND6_PROCESS_LOAD_START,    isr80h_command6_process_load_start);
    isr80h_register_command(SYSTEM_COMMAND7_INVOKE_SYSTEM_COMMAND, isr80h_command7_invoke_system_command);
    isr80h_register_command(SYSTEM_COMMAND8_GET_PROGRAM_ARGUMENTS, isr80h_command8_get_program_arguments);
    isr80h_register_command(SYSTEM_COMMAND9_EXIT,                  isr80h_command9_exit);
    isr80h_register_command(SYSTEM_COMMAND10_FOPEN,                isr80h_command10_fopen);
    isr80h_register_command(SYSTEM_COMMAND11_FCLOSE,               isr80h_command11_fclose);
    isr80h_register_command(SYSTEM_COMMAND12_FREAD,                isr80h_command12_fread);
    isr80h_register_command(SYSTEM_COMMAND13_FSEEK,                isr80h_command13_fseek);
    isr80h_register_command(SYSTEM_COMMAND14_FSTAT,                isr80h_command14_fstat);
    isr80h_register_command(SYSTEM_COMMAND15_REALLOC,              isr80h_command15_realloc);
    isr80h_register_command(SYSTEM_COMMAND16_WINDOW_CREATE,        isr80h_command16_window_create);
    isr80h_register_command(SYSTEM_COMMAND17_SYSOUT_TO_WINDOW,     isr80h_command17_sysout_to_window);
    isr80h_register_command(SYSTEM_COMMAND18_GET_WINDOW_EVENT,     isr80h_command18_get_window_event);
    isr80h_register_command(SYSTEM_COMMAND19_WINDOW_GRAPHICS_GET,  isr80h_command19_window_graphics_get);
    isr80h_register_command(SYSTEM_COMMAND20_GRAPHICS_PIXELS_BUFFER_GET, isr80h_command20_graphics_pixels_get);
    isr80h_register_command(SYSTEM_COMMAND21_WINDOW_REDRAW,        isr80h_command21_window_redraw);
    isr80h_register_command(SYSTEM_COMMAND22_GRAPHICS_CREATE,      isr80h_command22_graphics_create);
    isr80h_register_command(SYSTEM_COMMAND23_WINDOW_REDRAW_REGION, isr80h_command23_window_redraw_region);
    isr80h_register_command(SYSTEM_COMMAND24_UPDATE_WINDOW,        isr80h_command24_update_window);
    isr80h_register_command(SYSTEM_COMMAND25_UDELAY,               isr80h_command25_udelay);
    isr80h_register_command(SYSTEM_COMMAND26_E2E_MARK,              isr80h_command26_e2e_mark);
    isr80h_register_command(SYSTEM_COMMAND27_GETPID,               isr80h_command27_getpid);
    isr80h_register_command(SYSTEM_COMMAND28_WAITPID,              isr80h_command28_waitpid);
    isr80h_register_command(SYSTEM_COMMAND29_KILL,                 isr80h_command29_kill);
    isr80h_register_command(SYSTEM_COMMAND30_SLEEP_MS,             isr80h_command30_sleep_ms);
    isr80h_register_command(SYSTEM_COMMAND31_UPTIME,               isr80h_command31_uptime);
    isr80h_register_command(SYSTEM_COMMAND32_GETKEY_BLOCK,         isr80h_command32_getkey_block);
    isr80h_register_command(SYSTEM_COMMAND40_FWRITE,               isr80h_command40_fwrite);
    isr80h_register_command(SYSTEM_COMMAND41_FTRUNCATE,            isr80h_command41_ftruncate);
    isr80h_register_command(SYSTEM_COMMAND42_UNLINK,               isr80h_command42_unlink);
    isr80h_register_command(SYSTEM_COMMAND43_MKDIR,                isr80h_command43_mkdir);
    isr80h_register_command(SYSTEM_COMMAND44_RMDIR,                isr80h_command44_rmdir);
    isr80h_register_command(SYSTEM_COMMAND45_OPENDIR,              isr80h_command45_opendir);
    isr80h_register_command(SYSTEM_COMMAND46_READDIR,              isr80h_command46_readdir);
    isr80h_register_command(SYSTEM_COMMAND48_TIME,                 isr80h_command48_time);
    isr80h_register_command(SYSTEM_COMMAND49_SHUTDOWN,             isr80h_command49_shutdown);
    isr80h_register_command(SYSTEM_COMMAND50_REBOOT,               isr80h_command50_reboot);
    isr80h_register_command(SYSTEM_COMMAND51_CPU_STATS,            isr80h_command51_cpu_stats);
    isr80h_register_command(SYSTEM_COMMAND52_MEM_STATS,            isr80h_command52_mem_stats);
    isr80h_register_command(SYSTEM_COMMAND53_CPU_CURRENT,          isr80h_command53_cpu_current);
    isr80h_register_command(SYSTEM_COMMAND60_NET_INFO,             isr80h_command60_net_info);
    isr80h_register_command(SYSTEM_COMMAND61_NET_PING,             isr80h_command61_net_ping);
}
