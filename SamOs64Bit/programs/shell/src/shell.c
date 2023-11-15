// The SamOS shell: a line editor, builtins, quoting, and
// program launch.
//
// Builtins: help, echo (with > and >>), clear, exit, ls, cat,
// mkdir, write, rm, rmdir, date, shutdown, reboot. Anything else is
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
// samos_system returns the child's pid and samos_waitpid
// blocks the shell until that child exits (the design: "It waits for
// the child it starts"). The prompt comes back only after the child
// is gone, with its exit status. Before the scheduler work there was no wait
// syscall and the prompt came back while the child still ran.
// ---------------------------------------------------------------
#define SHELL_HAVE_WAIT_SYSCALL 1

static int shell_wait_for_child(const char* name, int child){
#if SHELL_HAVE_WAIT_SYSCALL
    int status = 0;
    if(samos_waitpid(child, &status) != child){
        printf("%s: wait failed\n", name);
        return 1;
    }
    printf("%s: exit status %d\n", name, status);
    return status;
#else
    (void)name;
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
static int shell_mkdir(int argc, char** argv);
static int shell_write(int argc, char** argv);
static int shell_rm(int argc, char** argv);
static int shell_rmdir(int argc, char** argv);
static int shell_date(int argc, char** argv);
static int shell_shutdown(int argc, char** argv);
static int shell_reboot(int argc, char** argv);

static const struct shell_builtin shell_builtins[] = {
    {"help",  "help              list the builtins",           shell_help},
    {"echo",  "echo [words...] [> file | >> file]  print or write the words", shell_echo},
    {"clear", "clear             clear the screen",            shell_clear},
    {"exit",  "exit              leave the shell",             shell_exit},
    {"ls",    "ls [dirs/files...] list a directory (default @:/)", shell_ls},
    {"cat",   "cat files...      print files",                 shell_cat},
    {"mkdir", "mkdir dirs...     make directories",            shell_mkdir},
    {"write", "write file words... write the words to a file", shell_write},
    {"rm",    "rm files...       remove files",                shell_rm},
    {"rmdir", "rmdir dirs...     remove empty directories",    shell_rmdir},
    {"date",     "date              print the date and time (UTC)", shell_date},
    {"shutdown", "shutdown          power off",                     shell_shutdown},
    {"reboot",   "reboot            restart the machine",           shell_reboot},
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

// Join argv[from..to) with single spaces and a final newline.
static void shell_join(char* out, int size, int from, int to, char** argv){
    int n = 0;
    out[0] = 0;
    for(int i = from; i < to && n < size; i++){
        n += snprintf(out + n, size - n, i > from ? " %s" : "%s", argv[i]);
    }
    if(n < size - 1){
        out[n++] = '\n';
        out[n] = 0;
    }
}

// Write text to a file opened with mode "w" (replace) or "a".
static int shell_write_text(const char* cmd, const char* name, const char* text,
                            const char* mode){
    char path[128];
    shell_path(name, path, sizeof(path));
    int fd = fopen(path, mode);
    if(fd < 0){
        printf("%s: %s: %s\n", cmd, name, samos_strerror(fd));
        return 1;
    }
    int len = strlen(text);
    int res = len ? fwrite(text, len, 1, fd) : 1;
    fclose(fd);
    if(res != 1){
        printf("%s: %s: %s\n", cmd, name, samos_strerror(res < 0 ? res : -SAMOS_ENOSPC));
        return 1;
    }
    return 0;
}

// "echo words > file" replaces the file, ">>"
// appends. The redirection is its own word or glued to the name
// (">file").
static int shell_echo(int argc, char** argv){
    char text[SHELL_LINE_MAX];
    for(int i = 1; i < argc; i++){
        if(argv[i][0] != '>'){
            continue;
        }
        int append = argv[i][1] == '>';
        const char* name = argv[i] + 1 + append;
        if(!*name){
            name = i + 1 < argc ? argv[i + 1] : NULL;
        }
        if(!name){
            printf("echo: missing file after >\n");
            return 1;
        }
        shell_join(text, sizeof(text), 1, i, argv);
        return shell_write_text("echo", name, text, append ? "a" : "w");
    }
    shell_join(text, sizeof(text), 1, argc, argv);
    print(text);
    return 0;
}

static int shell_write(int argc, char** argv){
    if(argc < 2){
        printf("usage: write file words...\n");
        return 1;
    }
    char text[SHELL_LINE_MAX];
    shell_join(text, sizeof(text), 2, argc, argv);
    return shell_write_text("write", argv[1], text, "w");
}

static int shell_clear(int argc, char** argv){
    print("\f");
    return 0;
}

// Unix seconds to a UTC date (civil-from-days, the inverse
// of the kernel's rtc_to_unix).
static int shell_date(int argc, char** argv){
    long t = samos_time();
    if(t < 0){
        printf("date: no clock\n");
        return 1;
    }
    long days = t / 86400, secs = t % 86400;
    long z = days + 719468;
    long era = z / 146097;
    long doe = z - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    int day = (int)(doy - (153 * mp + 2) / 5 + 1);
    int month = (int)(mp < 10 ? mp + 3 : mp - 9);
    int year = (int)(yoe + era * 400 + (month <= 2));
    printf("%d-%02d-%02d %02d:%02d:%02d UTC\n", year, month, day,
           (int)(secs / 3600), (int)(secs / 60 % 60), (int)(secs % 60));
    return 0;
}

static int shell_shutdown(int argc, char** argv){
    printf("shutting down\n");
    samos_shutdown();
    return 1;
}

static int shell_reboot(int argc, char** argv){
    printf("rebooting\n");
    samos_reboot();
    return 1;
}

static int shell_exit(int argc, char** argv){
    printf("bye\n");
    samos_exit(0);
    return 0;
}

// ls walks the directory with opendir/readdir. The
// first version probed a fixed list of file names (no readdir yet).
static void shell_ls_line(const char* name, uint32_t size, int is_dir){
    if(is_dir){
        printf("%-14s    <DIR>\n", name);
    } else {
        printf("%-14s %8u\n", name, size);
    }
}

// One argument: a directory is listed (with a "name:" header when
// there are several arguments), a file gets its own line.
static int shell_ls_one(const char* name, int header){
    char path[128];
    shell_path(name, path, sizeof(path));
    int fd = opendir(path);
    if(fd > 0){
        struct file_dirent entry;
        int res;
        if(header){
            printf("%s:\n", name);
        }
        while((res = readdir(fd, &entry)) == 1){
            shell_ls_line(entry.name, entry.size, entry.flags & FILE_DIRENT_DIRECTORY);
        }
        closedir(fd);
        if(res < 0){
            printf("ls: %s: %s\n", name, samos_strerror(res));
            return -1;
        }
        return 0;
    }
    if(fd == -SAMOS_ENOTDIR){
        fd = fopen(path, "r");
    }
    struct file_stat st = {0};
    if(fd < 0 || fstat(fd, &st) < 0){
        printf("ls: %s: %s\n", name, samos_strerror(fd < 0 ? fd : -SAMOS_EIO));
        if(fd > 0){
            fclose(fd);
        }
        return -1;
    }
    shell_ls_line(name, st.filesize, 0);
    fclose(fd);
    return 0;
}

static int shell_ls(int argc, char** argv){
    if(argc < 2){
        return shell_ls_one("@:/", 0) < 0;
    }
    int res = 0;
    for(int i = 1; i < argc; i++){
        if(shell_ls_one(argv[i], argc > 2) < 0){
            res = 1;
        }
    }
    return res;
}

static int shell_cat_one(const char* name){
    char path[128];
    shell_path(name, path, sizeof(path));
    int fd = fopen(path, "r");
    if(fd <= 0){
        printf("cat: %s: %s\n", name, samos_strerror(fd));
        return -1;
    }
    struct file_stat st = {0};
    if(fstat(fd, &st) < 0 || (st.flags & FILE_STAT_DIRECTORY)){
        printf("cat: %s: is a directory\n", name);
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

// Mkdir, rm, rmdir: one syscall per argument,
// "cmd: name: reason" on failure.
static int shell_each_path(int argc, char** argv, int (*op)(const char* path)){
    if(argc < 2){
        printf("usage: %s names...\n", argv[0]);
        return 1;
    }
    int res = 0;
    for(int i = 1; i < argc; i++){
        char path[128];
        shell_path(argv[i], path, sizeof(path));
        int r = op(path);
        if(r < 0){
            printf("%s: %s: %s\n", argv[0], argv[i], samos_strerror(r));
            res = 1;
        }
    }
    return res;
}

static int shell_mkdir(int argc, char** argv){
    return shell_each_path(argc, argv, mkdir);
}

static int shell_rm(int argc, char** argv){
    return shell_each_path(argc, argv, unlink);
}

static int shell_rmdir(int argc, char** argv){
    return shell_each_path(argc, argv, rmdir);
}

static int shell_run_program(struct command_argument* command){
    int child = samos_system(command);
    if(child < 0){
        printf("%s: command not found\n", command->argument);
        return 127;
    }
    return shell_wait_for_child(command->argument, child);
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
