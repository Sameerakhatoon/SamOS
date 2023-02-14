# G08 - interrupt_handler reloads kernel segments / saves task state on every IRQ, even when the trap came from kernel mode

## Where it lives

`src/idt/idt.c::interrupt_handler`.

## What the book ships

```c
void interrupt_handler(int interrupt, struct interrupt_frame* frame){
    kernel_page();                       // always reload DS/ES/FS/GS + CR3
    if(callbacks[interrupt]){
        task_current_save_state(frame);  // always copy frame->{ip,cs,ss,esp,...}
        callbacks[interrupt](frame);
    }
    task_page();                         // always reload user paging
    outb(0x20, 0x20);
}
```

That works as long as IRQs only fire from user mode (which is the case until Ch 150 registers `idt_clock` on IRQ0). After Ch 150 the PIT starts firing from kernel mode too, and TWO things break:

1. **`task_current_save_state` corrupts `task->registers.ss/esp`**. On a kernel-mode IRQ the CPU does NOT push SS/ESP (no privilege change), so `frame->ss` and `frame->esp` contain whatever was at those stack slots before. `task_save_state` copies that garbage into `task->registers`. The next `task_return` does `mov ax, [ebx+44]; mov ds, ax` with the bogus SS, and the CPU #GPs on the segment load.

2. **`kernel_page()` calls `kernel_registers()` which clobbers AX in the middle of unrelated kernel code**. The trap return path's `popad` restores the original EAX, BUT if a hardware IRQ is delivered between the `mov ax, 0x10` and the segment-load instructions inside `kernel_registers`, the iret returns to mid-`kernel_registers` and the segment loads use whatever AX has now (a heap pointer like `0x01829000` → selector `0x9000` → #GP).

Both manifest as an infinite #GP loop at kernel IP `0x100038` (`mov fs, ax` inside `kernel_registers`) that grows the stack and eventually triple-faults.

## How to surface it

After Ch 150's `idt_register_interrupt_callback(0x20, idt_clock)` lands, boot the kernel. Within milliseconds of `enable_interrupts()` the PIT fires from kernel mode, the bug triggers, and most boot smoke probes never make it to VGA - they're scrolled off by the eventual triple-fault loop.

## The fix

Gate the whole kernel-context fix-up + save-state work on "did the trap come from user mode?" using `(frame->cs & 0x3) == 0x3`. For kernel-mode interrupts the segments and CR3 are already kernel-side, `frame->ss/esp` are not valid, and the user task wasn't running so there's nothing to save.

```c
void interrupt_handler(int interrupt, struct interrupt_frame* frame){
    int from_user = ((frame->cs & 0x3) == 0x3);
    if(from_user){
        kernel_page();
    }
    if(callbacks[interrupt] && task_current()){
        if(from_user){
            task_current_save_state(frame);
        }
        callbacks[interrupt](frame);
    }
    if(from_user && task_current()){
        task_page();
    }
    outb(0x20, 0x20);
}
```

This subsumes G05 (don't `task_page` with null current_task) and G07 (don't `task_current_save_state` with null current_task) - the from-user-mode gate already implies user code was running and therefore a current task exists. We keep the `task_current()` checks as a belt-and-suspenders second guard.

## Result

The 11 broken tests at the Ch 150 commit dropped to 5 after G07, then to **0** after G08. Suite restores to 32/32, the boot smoke probes paint correctly, and the multitasking demo runs in the now-uncluttered VGA frame.

## Surfaced

Chapter 150 (timer-driven multitasking).

## Fix commit

`g08: only do kernel-context fix-up in interrupt_handler when the trap came from user mode`.
