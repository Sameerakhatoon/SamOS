// The shell's builtins compiled by SamCompiler, run without a
// keyboard (aarch64 and riscv64 have none before the aarch64 device work/the riscv64 device work):
//   SHTEST.ELF help + echo hi > A.TXT + cat A.TXT
//   SHTEST.ELF @SHTEST.TXT        (one shell line per line of the file)
// Each line is parsed by samos_parse_command and run by the shell's
// own shell_execute, as the shell's prompt loop does. shell.c is
// included whole with its main renamed, so every builtin is the
// shell's code.
#define main shell_main
#include "../shell/src/shell.c"
#undef main

static void shtest_line(char* line){
    printf("shtest> %s\n", line);
    struct command_argument* command = samos_parse_command(line, SHELL_LINE_MAX);
    if(command){
        printf("shtest: status %d\n", shell_execute(command));
        samos_free_command(command);
    }
}

// The kernel command line holds 255 characters: a longer script is a
// file on the boot disk.
static int shtest_file(const char* name){
    char path[128];
    char text[2048];
    shell_path(name, path, sizeof(path));
    int fd = fopen(path, "r");
    struct file_stat st = {0};
    if(fd <= 0 || fstat(fd, &st) < 0 || st.filesize >= sizeof(text) || fread(text, st.filesize, 1, fd) != 1){
        printf("shtest: cannot read %s\n", name);
        return 1;
    }
    fclose(fd);
    text[st.filesize] = 0;
    char* line = text;
    for(char* p = text; *p; p++){
        if(*p == '\n'){
            *p = 0;
            shtest_line(line);
            line = p + 1;
        }
    }
    return 0;
}

int main(int argc, char** argv){
    if(argc == 2 && argv[1][0] == '@'){
        return shtest_file(argv[1] + 1);
    }
    char line[SHELL_LINE_MAX];
    int i = 1;
    while(i < argc){
        int n = 0;
        line[0] = 0;
        while(i < argc && strcmp(argv[i], "+") != 0){
            n += snprintf(line + n, sizeof(line) - n, n ? " %s" : "%s", argv[i]);
            i++;
        }
        i++;
        shtest_line(line);
    }
    return 0;
}
