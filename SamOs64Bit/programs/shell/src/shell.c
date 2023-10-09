// The SamOS shell: a line editor, builtins, quoting, and
// program launch.
//
// Builtins: help, echo, clear, exit, ls, cat. Anything else is
// looked up on the boot filesystem and started as a program.

#include "shell.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "samos.h"
#include "file.h"

#define SHELL_LINE_MAX   1024
#define SHELL_ARGV_MAX   32
#define SHELL_CAT_CHUNK  512

// The kernel's FILE_SEEK_MODE (src/fs/file.h).
#define SHELL_SEEK_SET   0

// ---------------------------------------------------------------
// Waiting for the child.
//
// STUB(core): the kernel has no wait syscall yet, so
// samos_system returns as soon as the child is scheduled and the
// shell prints its next prompt while the child is still running.
// When the core session lands wait (the design: "It waits for the
// child it starts"), set SHELL_HAVE_WAIT_SYSCALL to 1 and make
// shell_wait_for_child call it with the id samos_system returns.
// ---------------------------------------------------------------
#define SHELL_HAVE_WAIT_SYSCALL 0

static int shell_wait_for_child(int child){
#if SHELL_HAVE_WAIT_SYSCALL
    return samos_wait(child);
#else
    (void)child;
    return 0;
#endif
}

struct shell_builtin {
    const char* name;
    const char* usage;
    int (*run)(int argc, char** argv);
};

static int shell_help(int argc, char** argv);
static int shell_echo(int argc, char** argv);
static int shell_clear(int argc, char** argv);
static int shell_exit(int argc, char** argv);
static int shell_ls(int argc, char** argv);
static int shell_cat(int argc, char** argv);

static const struct shell_builtin shell_builtins[] = {
    {"help",  "help              list the builtins",           shell_help},
    {"echo",  "echo [words...]   print the words",             shell_echo},
    {"clear", "clear             clear the screen",            shell_clear},
    {"exit",  "exit              leave the shell",             shell_exit},
    {"ls",    "ls [files...]     list files with their sizes", shell_ls},
    {"cat",   "cat files...      print files",                 shell_cat},
};
#define SHELL_BUILTIN_COUNT (sizeof(shell_builtins) / sizeof(shell_builtins[0]))

// "hello.txt" -> "@:/hello.txt"; paths that already name a drive
// ("@:/x", "0:/x") are kept as they are.
static void shell_path(const char* name, char* out, int size){
    if(strchr(name, ':')){
        snprintf(out, size, "%s", name);
    } else if(name[0] == '/'){
        snprintf(out, size, "@:%s", name);
    } else {
        snprintf(out, size, "@:/%s", name);
    }
}

static int shell_help(int argc, char** argv){
    printf("SamOS shell builtins:\n");
    for(size_t i = 0; i < SHELL_BUILTIN_COUNT; i++){
        printf("  %s\n", shell_builtins[i].usage);
    }
    printf("Anything else runs a program from the disk, e.g. hello.elf a b\n");
    printf("Quote words with spaces: echo \"a b\" 'c d'\n");
    return 0;
}

static int shell_echo(int argc, char** argv){
    for(int i = 1; i < argc; i++){
        printf(i > 1 ? " %s" : "%s", argv[i]);
    }
    printf("\n");
    return 0;
}

static int shell_clear(int argc, char** argv){
    print("\f");
    return 0;
}

static int shell_exit(int argc, char** argv){
    printf("bye\n");
    samos_exit();
    return 0;
}

// Prints "<name>  <size>" or an error. Returns 0 if the file exists.
static int shell_ls_one(const char* name){
    char path[128];
    shell_path(name, path, sizeof(path));
    int fd = fopen(path, "r");
    if(fd <= 0){
        printf("ls: %s: not found\n", name);
        return -1;
    }
    struct file_stat st = {0};
    if(fstat(fd, &st) < 0){
        printf("%-14s  (directory)\n", name);
    } else {
        printf("%-14s %8u\n", name, st.filesize);
    }
    fclose(fd);
    return 0;
}

static int shell_ls(int argc, char** argv){
    if(argc > 1){
        int res = 0;
        for(int i = 1; i < argc; i++){
            if(shell_ls_one(argv[i]) < 0){
                res = 1;
            }
        }
        return res;
    }

    // STUB(storage/core): there is no readdir syscall, so a
    // bare `ls` probes the files the build stages on the boot disk
    // (see the Makefile mcopy lines and build.sh) and lists the
    // ones that exist. Replace with a real directory walk when a
    // readdir syscall lands.
    static const char* known_files[] = {
        "SHELL.ELF", "BLANK.ELF", "HELLO.ELF", "SIMPLE.BIN", "HELLO.TXT",
        "SYSFONT.BMP", "CLSICON.BMP",
    };
    for(size_t i = 0; i < sizeof(known_files) / sizeof(known_files[0]); i++){
        char path[64];
        shell_path(known_files[i], path, sizeof(path));
        int fd = fopen(path, "r");
        if(fd <= 0){
            continue;
        }
        struct file_stat st = {0};
        fstat(fd, &st);
        printf("%-14s %8u\n", known_files[i], st.filesize);
        fclose(fd);
    }
    return 0;
}

static int shell_cat_one(const char* name){
    char path[128];
    shell_path(name, path, sizeof(path));
    int fd = fopen(path, "r");
    if(fd <= 0){
        printf("cat: %s: not found\n", name);
        return -1;
    }
    struct file_stat st = {0};
    if(fstat(fd, &st) < 0){
        printf("cat: %s: not a file\n", name);
        fclose(fd);
        return -1;
    }

    // Seek to each chunk explicitly. Written when the kernel's fread
    // did not advance the position; since the storage work it does, so the seek
    // is redundant but harmless.
    char chunk[SHELL_CAT_CHUNK + 1];
    uint32_t offset = 0;
    int res = 0;
    while(offset < st.filesize){
        uint32_t n = st.filesize - offset;
        if(n > SHELL_CAT_CHUNK){
            n = SHELL_CAT_CHUNK;
        }
        if(fseek(fd, offset, SHELL_SEEK_SET) < 0 || fread(chunk, n, 1, fd) != 1){
            printf("\ncat: %s: read error\n", name);
            res = -1;
            break;
        }
        // print() stops at a NUL, so hand embedded NULs to putchar.
        uint32_t start = 0;
        for(uint32_t i = 0; i <= n; i++){
            if(i == n || chunk[i] == '\0'){
                char saved = chunk[i];
                chunk[i] = '\0';
                print(chunk + start);
                chunk[i] = saved;
                start = i + 1;
            }
        }
        offset += n;
    }
    fclose(fd);
    return res;
}

static int shell_cat(int argc, char** argv){
    if(argc < 2){
        printf("usage: cat files...\n");
        return 1;
    }
    int res = 0;
    for(int i = 1; i < argc; i++){
        if(shell_cat_one(argv[i]) < 0){
            res = 1;
        }
    }
    return res;
}

static int shell_run_program(struct command_argument* command){
    int child = samos_system(command);
    if(child < 0){
        printf("%s: command not found\n", command->argument);
        return 127;
    }
    return shell_wait_for_child(child);
}

// Run one parsed line. Exposed for the selftest build.
int shell_execute(struct command_argument* command){
    char* argv[SHELL_ARGV_MAX];
    int argc = 0;
    for(struct command_argument* a = command; a && argc < SHELL_ARGV_MAX; a = a->next){
        argv[argc++] = a->argument;
    }
    for(size_t i = 0; i < SHELL_BUILTIN_COUNT; i++){
        if(strcmp(argv[0], shell_builtins[i].name) == 0){
            return shell_builtins[i].run(argc, argv);
        }
    }
    return shell_run_program(command);
}

int main(int argc, char** argv){
    print("SamOs v1.0.0 - type help\n");
    char buf[SHELL_LINE_MAX];
    while(1){
        print("> ");
        samos_terminal_readline(buf, sizeof(buf), true);
        print("\n");
        struct command_argument* command = samos_parse_command(buf, sizeof(buf));
        if(!command){
            // Empty line, or an unterminated quote.
            if(strchr(buf, '"') || strchr(buf, '\'')){
                printf("shell: unterminated quote\n");
            }
            continue;
        }
        shell_execute(command);
        samos_free_command(command);
    }
    return 0;
}
