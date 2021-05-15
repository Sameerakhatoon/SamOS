section .asm

extern int21h_handler
extern no_interrupt_handler
extern isr80h_handler

global idt_load
global int21h
global no_interrupt
global isr80h_wrapper
global enable_interrupts
global disable_interrupts

idt_load:
    push ebp
    mov ebp, esp
    mov ebx, [ebp+8]
    lidt [ebx]
    pop ebp
    ret

; Ch 102: drop the cli/sti pair. With ring-3 callers in the picture
; the CPU already turns off the IF bit at interrupt entry (we set up
; the IDT gates as interrupt gates, not trap gates), and the iret at
; the end pops it back to whatever the user code had.
int21h:
    pushad
    call int21h_handler
    popad
    iret

no_interrupt:
    pushad
    call no_interrupt_handler
    popad
    iret

; Ch 102 isr80h_wrapper - the int 0x80 entry point user processes will
; call to ask the kernel to do something. The CPU has already pushed
; the user-side ip/cs/flags/esp/ss onto the kernel stack when this
; runs.
isr80h_wrapper:
    ; Push the user general-purpose registers in the order C's
    ; struct interrupt_frame expects: edi, esi, ebp, esp(reserved),
    ; ebx, edx, ecx, eax.
    pushad

    ; Pass the address of the interrupt frame (current ESP) as the
    ; second argument to the C handler.
    push esp

    ; Pass the command number (the user's pre-trap EAX, which pushad
    ; already saved at offset 28 of the frame).
    push eax

    call isr80h_handler

    ; Stash the handler's return value before popad clobbers EAX.
    mov dword [tmp_res], eax

    ; Pop the two arguments we pushed.
    add esp, 8

    ; Restore the user GPRs.
    popad

    ; Reload EAX with the syscall return value so the user code sees it.
    mov eax, [tmp_res]
    iretd

enable_interrupts:
    sti
    ret

disable_interrupts:
    cli
    ret

section .data
; Stash for isr80h_handler's return value across popad.
tmp_res: dd 0
