#include "samos.h"
#include "stdlib.h"
#include "stdio.h"
#include "string.h"

// Multitasking-demo dispatcher driven by argv[0]. The "control" tasks
// loop forever (Testing!/Abc! prove the scheduler keeps switching). The
// other branches each exercise one Ch 145-149 user-visible code path:
//
//   CRASH-* -> print marker, null-deref to trip idt_handle_exception
//   EXIT-*  -> print marker, return from main; c_start calls samos_exit
//   BS-*    -> print "BS-ABC\b\b\bXYZ" so terminal_backspace rewrites
//              the row in place; tests grep for "BS-XYZ"
int main(int argc, char** argv){
    if(argc < 1 || !argv[0]){
        print("NO-ARGV\n");
        while(1){}
        return 0;
    }
    if(strncmp(argv[0], "CRASH", 5) == 0){
        print("CRASH-RAN\n");
        volatile int* nullp = (volatile int*)0;
        *nullp = 0x42;
        return 0;
    }
    if(strncmp(argv[0], "EXIT", 4) == 0){
        print("EXIT-RAN\n");
        return 0;
    }
    if(strncmp(argv[0], "BS", 2) == 0){
        print("BS-ABC\b\b\bXYZ\n");
        return 0;
    }
    if(strncmp(argv[0], "MF", 2) == 0){
        // Ch 130/131: exercise the user-side malloc + free path. Two
        // distinct allocations + free of the first + reuse of its
        // slot give the same address pattern the kernel side already
        // verifies; printing "MF-OK" proves the syscall round-tripped
        // without crashing.
        char* a = samos_malloc(100);
        char* b = samos_malloc(8000);
        if(!a || !b){ print("MF-NULL\n"); return 0; }
        a[0] = 'x';
        b[0] = 'y';
        samos_free(a);
        char* c = samos_malloc(100);
        if(!c){ print("MF-NULL2\n"); return 0; }
        print("MF-OK\n");
        return 0;
    }
    if(strncmp(argv[0], "PC", 2) == 0){
        // Ch 109: putchar syscall (cmd 3) sends one byte through
        // isr80h_command3_putchar. Print "PC-" then three putchar
        // calls then a newline, so tests can assert PC-Xyz on serial.
        print("PC-");
        samos_putchar('X');
        samos_putchar('y');
        samos_putchar('z');
        samos_putchar('\n');
        return 0;
    }
    if(strncmp(argv[0], "IT", 2) == 0){
        // Ch 133: stdlib itoa renders an integer in decimal ASCII.
        // The book's signature is `char* itoa(int)` returning a
        // pointer to an internal buffer. Print as "IT:8763\n" so
        // test 53 can grep the result.
        print("IT:");
        print(itoa(8763));
        print("\n");
        return 0;
    }
    if(strncmp(argv[0], "PF", 2) == 0){
        // Ch 135: stdlib printf with %i and %s. Test 54 greps the
        // resulting "PF:42=42 hi" on serial.
        printf("PF:%i=%i %s\n", 42, 42, "hi");
        return 0;
    }
    if(strncmp(argv[0], "RL", 2) == 0){
        // Ch 136: stdlib samos_terminal_readline reads chars via
        // samos_getkeyblock until '\n' or buf is full. With
        // output_while_typing=true it putchars each accepted char so
        // tests can see input arriving even if the line never
        // terminates (test 55 watches for the echoed bytes).
        print("RL-START\n");
        char buf[8] = { 0 };
        samos_terminal_readline(buf, 7, true);
        print("\nRL-DONE:");
        print(buf);
        print("\n");
        while(1){}
        return 0;
    }
    if(strncmp(argv[0], "SH", 2) == 0){
        // Ch 145: cmd 7 SYSTEM_COMMAND7_INVOKE_SYSTEM_COMMAND parses
        // the user-supplied command into argument structs, loads the
        // named program, and task_returns into it with argv injected.
        // We invoke "blank.elf SHRAN" so the new task gets argv[0]=
        // "blank.elf", falls through blank.c's branch ladder, and ends
        // up in the default `print(argv[0])` loop printing "blank.elf"
        // forever on serial.
        print("SH-CALL\n");
        samos_system_run("blank.elf SHRAN");
        print("SH-RETURN\n");
        while(1){}
        return 0;
    }
    if(strncmp(argv[0], "LD", 2) == 0){
        // Ch 138: cmd 6 SYSTEM_COMMAND6_PROCESS_LOAD_START loads a new
        // ELF and switches to it. Print "LD-CALL" so the test knows we
        // got this far, then ask the kernel to load blank.elf again
        // (which will land in the NO-ARGV branch and print "NO-ARGV").
        // After the call returns (it returns immediately in our impl)
        // print "LD-RETURN" so test 51 can confirm the syscall didn't
        // crash either side.
        print("LD-CALL\n");
        samos_process_load_start("0:/blank.elf");
        print("LD-RETURN\n");
        while(1){}
        return 0;
    }
    if(strncmp(argv[0], "KEY", 3) == 0){
        // Ch 116/149: forever-read keys via samos_getkeyblock (cmd 2
        // wrapped in a wait loop) and echo each one as "K:<c>". This
        // task lets test 50 verify caps-lock case toggling end-to-end:
        // the classic keyboard driver's caps_lock state is global, so
        // ANY key after caps_lock arrives uppercase, even if it lands
        // in another task's buffer.
        char buf[4] = { 'K', ':', 0, '\n' };
        while(1){
            int c = samos_getkeyblock();
            buf[2] = (char)c;
            samos_putchar(buf[0]);
            samos_putchar(buf[1]);
            samos_putchar(buf[2]);
            samos_putchar(buf[3]);
        }
        return 0;
    }
    while(1){
        print(argv[0]);
    }
    return 0;
}
