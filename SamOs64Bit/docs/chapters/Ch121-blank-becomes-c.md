# Ch 127 - Blank program becomes a C program; stdlib scaffold

The user-space program switches from `blank.asm` to `blank.c`. A new `programs/stdlib/` directory hosts what will grow into a tiny C standard library; for now it ships only `start.asm` with the `_start` symbol that invokes `main()`.

## What landed

- `programs/stdlib/src/start.asm` - global `_start: call main; ret` in section `.asm`.
- `programs/stdlib/Makefile` - assembles `start.o`, links to a relocatable `stdlib.elf` (linked into user programs as a shared object alongside their own code).
- `programs/blank/blank.c` - `int main(int argc, char** argv) { return 0; }`.
- `programs/blank/Makefile` - now compiles `blank.c` to `blank.o` and links it with `../stdlib/stdlib.elf` (both for the flat `blank.bin` and the `blank.elf` targets). Old `blank.asm` is preserved but unused.
- Root `Makefile` - `user_programs` and `user_programs_clean` recurse into `programs/stdlib` before `programs/blank`.

## Book quirk shipped here

The book is explicit: with only `return 0;` in `main`, `_start`'s `ret` falls off the end of an uninitialized user stack. The CPU jumps to garbage, page-faults, and (since no page-fault callback is registered) loops the IDT until it triple-faults. QEMU with `-no-reboot` exits the moment that happens, which kills the monitor before any `pmemsave` can grab VGA. So **every QEMU-driven test goes red after this commit** even though boot itself is fine - the kernel actually paints "Hello world!", all smoke probes, and reaches `task_run_first_ever_task` before the user task crashes.

The book notes this: *"Why loop forever? As explained earlier the ret instruction in the start symbol will not function correctly because we have no return address on the user stack, until we replace it with an EXIT command we will need to ensure it never gets executed."*

## What fixes this

Ch 128 adds `print` to stdlib and rewrites `main` to call `print(...)` then `while(1) {}` - the infinite loop prevents the fatal `ret`. Tests come back green at Ch 128.
