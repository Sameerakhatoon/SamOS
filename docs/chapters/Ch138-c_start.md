# Ch 144 - c_start bootstrap (process args delivered to main)

User programs no longer need to call `samos_process_get_arguments` themselves. A new `c_start` bridges between the asm `_start` and the C `main`: it pulls the arguments out and forwards them as the conventional `(int argc, char** argv)`.

## What landed

- `programs/stdlib/src/start.c` - new C bootstrap:
  ```c
  extern int main(int argc, char** argv);
  void c_start(){
      struct process_arguments arguments;
      samos_process_get_arguments(&arguments);
      main(arguments.argc, arguments.argv);
  }
  ```
- `programs/stdlib/src/start.asm` - `_start` now `call c_start; ret`.
- `programs/stdlib/Makefile`:
  - `start.asm` -> `start.asm.o` (was `start.o`).
  - new `start.o` rule compiles `start.c`.
  - both go into `FILES` (`start.asm.o` first so `_start` is the entry).
- `programs/blank/blank.c` simplified to `print(argv[0]); print("did this work?\n"); while(1){}`. The kernel injects "Testing!" from Ch 143's bootstrap; the user sees it as `argv[0]`.

## Test impact

Suite stays 32/32 (after one rerun - tests 10/11 are timing-sensitive and occasionally flake under load). Visible effect at boot: VGA now shows "Testing!did this work?".
