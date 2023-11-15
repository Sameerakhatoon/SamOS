# Lecture 170 - bind window redraw to ISR80H

Upstream: PeachOS64BitCourse f99c687.

Single line of code: register the L167 body in
`isr80h_register_commands`:

    isr80h_register_command(SYSTEM_COMMAND21_WINDOW_REDRAW,
                            isr80h_command21_window_redraw);

After this commit userspace's `peachos_window_redraw` actually
reaches the kernel.

## BIOS test status

Source + link. Build links.
