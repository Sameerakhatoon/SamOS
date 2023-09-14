// Host-side unit test for the userland libc and the shell's
// parser. run.sh compiles programs/stdlib/src/*.c for the host,
// prefixes every symbol with sam_ (objcopy) so nothing clashes with
// glibc, and links this file against them. The few syscalls the
// code under test needs are faked below.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// ---- fakes for the syscall trampolines -------------------------
static char console[4096];
static size_t console_len;
static int allocs_live;

void sam_print(const char* s){
    size_t n = strlen(s);
    memcpy(console + console_len, s, n);
    console_len += n;
    console[console_len] = 0;
}
void sam_samos_putchar(char c){ console[console_len++] = c; console[console_len] = 0; }
void* sam_samos_malloc(size_t n){ allocs_live++; return calloc(1, n); }
void sam_samos_free(void* p){ if(p) allocs_live--; free(p); }
void* sam_samos_realloc(void* p, size_t n){ return realloc(p, n); }
int sam_samos_system(void* args){ return 0; }

static const char* keys;
int sam_samos_getkey(void){ return *keys ? *keys++ : '\r'; }
void sam_samos_udelay(unsigned long us){ }

// Unused trampolines referenced by the objects.
#define UNUSED_TRAMPOLINE(name) long sam_##name(void){ return 0; }
UNUSED_TRAMPOLINE(samos_process_get_arguments)
UNUSED_TRAMPOLINE(samos_exit)
UNUSED_TRAMPOLINE(samos_fopen)
UNUSED_TRAMPOLINE(samos_fclose)
UNUSED_TRAMPOLINE(samos_fread)
UNUSED_TRAMPOLINE(samos_fseek)
UNUSED_TRAMPOLINE(samos_fstat)

// ---- the code under test ---------------------------------------
int   sam_snprintf(char*, size_t, const char*, ...);
int   sam_printf(const char*, ...);
long  sam_strtol(const char*, char**, int);
unsigned long sam_strtoul(const char*, char**, int);
int   sam_atoi(const char*);
char* sam_itoa(int);
char* sam_itoa_r(int, char*, size_t, int);
void* sam_calloc(size_t, size_t);
char* sam_strchr(const char*, int);
char* sam_strrchr(const char*, int);
char* sam_strstr(const char*, const char*);
char* sam_strcat(char*, const char*);
char* sam_strncat(char*, const char*, int);
int   sam_strcmp(const char*, const char*);
char* sam_strtok(char*, const char*);
extern int sam_errno;

struct command_argument { char argument[512]; struct command_argument* next; };
struct command_argument* sam_samos_parse_command(const char*, int);
void sam_samos_free_command(struct command_argument*);
void sam_samos_terminal_readline(char*, int, int);
int  sam_samos_system_run(const char*);

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if(!(cond)){ failures++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while(0)

#define FMT(want, ...) do { char b_[256]; int n_ = sam_snprintf(b_, sizeof b_, __VA_ARGS__); \
    CHECK(strcmp(b_, want) == 0 && n_ == (int)strlen(want), \
          "snprintf(%s) = \"%s\" (%d), want \"%s\"", #__VA_ARGS__, b_, n_, want); } while(0)

static void test_printf(void){
    FMT("42", "%d", 42);
    FMT("-42", "%i", -42);
    FMT("-2147483648", "%d", (int)0x80000000);
    FMT("4294967295", "%u", 0xFFFFFFFFu);
    FMT("ff", "%x", 255);
    FMT("FF", "%X", 255);
    FMT("0x1234", "%p", (void*)0x1234);
    FMT("A", "%c", 'A');
    FMT("hi", "%s", "hi");
    FMT("(null)", "%s", (char*)NULL);
    FMT("-9223372036854775808", "%ld", (long)(-9223372036854775807L - 1));
    FMT("18446744073709551615", "%lu", 18446744073709551615UL);
    FMT("123456789012", "%lld", 123456789012LL);
    FMT("[   7]", "[%4d]", 7);
    FMT("[7   ]", "[%-4d]", 7);
    FMT("[0007]", "[%04d]", 7);
    FMT("[-007]", "[%04d]", -7);
    FMT("[  ab]", "[%4s]", "ab");
    FMT("[ab  ]", "[%-4s]", "ab");
    FMT("[  x]", "[%*c]", 3, 'x');
    FMT("[000000ff]", "[%08x]", 255);
    FMT("100%", "100%%");
    FMT("50%", "50%");          // trailing lone % (was a read past the end)
    FMT("%q", "%q");
    FMT("a 1 b", "%s %d %s", "a", 1, "b");

    // Truncation: C99 return value, always terminated.
    char small[5];
    int n = sam_snprintf(small, sizeof small, "abcdefgh");
    CHECK(n == 8 && strcmp(small, "abcd") == 0, "truncate: %d \"%s\"", n, small);
    n = sam_snprintf(NULL, 0, "%d", 12345);
    CHECK(n == 5, "size-0 count: %d", n);

    console_len = 0;
    n = sam_printf("x=%d y=%s\n", 3, "z");
    CHECK(strcmp(console, "x=3 y=z\n") == 0 && n == 8, "printf console \"%s\" %d", console, n);
    // Longer than the 128-byte batch buffer.
    console_len = 0;
    sam_printf("%0200d|", 1);
    CHECK(console_len == 201 && console[199] == '1' && console[200] == '|', "printf flush len %zu", console_len);
}

static void test_numbers(void){
    char* end;
    CHECK(sam_atoi("123") == 123, "atoi 123");
    CHECK(sam_atoi("  -45x") == -45, "atoi -45");
    CHECK(sam_atoi("+7") == 7, "atoi +7");
    CHECK(sam_atoi("abc") == 0, "atoi abc");
    CHECK(sam_strtol("0x1F", &end, 0) == 31 && *end == 0, "strtol 0x1F");
    CHECK(sam_strtol("1f", &end, 16) == 31, "strtol 1f/16");
    CHECK(sam_strtol("017", &end, 0) == 15, "strtol octal");
    CHECK(sam_strtol("z", &end, 36) == 35, "strtol base36");
    CHECK(sam_strtol("12ab", &end, 10) == 12 && strcmp(end, "ab") == 0, "strtol endptr");
    const char* nothing = "  xyz";
    sam_strtol(nothing, &end, 10);
    CHECK(end == nothing, "strtol no digits -> endptr = str");
    sam_errno = 0;
    CHECK(sam_strtol("99999999999999999999", &end, 10) == 0x7FFFFFFFFFFFFFFFL && sam_errno == 34,
          "strtol overflow saturates + ERANGE (errno %d)", sam_errno);
    sam_errno = 0;
    CHECK(sam_strtol("-9223372036854775808", &end, 10) == (-0x7FFFFFFFFFFFFFFFL - 1) && sam_errno == 0,
          "strtol LONG_MIN exact");
    CHECK(sam_strtoul("ffffffffffffffff", &end, 16) == 0xFFFFFFFFFFFFFFFFUL, "strtoul max");

    char a[12], b[12];
    char* pa = sam_itoa_r(-2147483647 - 1, a, sizeof a, 10);
    char* pb = sam_itoa_r(255, b, sizeof b, 16);
    CHECK(pa && strcmp(a, "-2147483648") == 0, "itoa_r INT_MIN \"%s\"", a);
    CHECK(pb && strcmp(b, "ff") == 0, "itoa_r hex");
    CHECK(strcmp(a, "-2147483648") == 0, "itoa_r buffers independent");
    CHECK(sam_itoa_r(12345, a, 3, 10) == NULL, "itoa_r too small -> NULL");
    CHECK(strcmp(sam_itoa(123), "123") == 0, "itoa 123");
    CHECK(strcmp(sam_itoa(0), "0") == 0, "itoa 0");

    sam_errno = 0;
    CHECK(sam_calloc((size_t)1 << 62, 8) == NULL && sam_errno == 12, "calloc overflow -> NULL/ENOMEM");
    int* z = sam_calloc(4, sizeof(int));
    CHECK(z && z[0] == 0 && z[3] == 0, "calloc zeroed");
    free(z);
    allocs_live--;
}

static void test_strings(void){
    const char* s = "hello world";
    CHECK(sam_strchr(s, 'o') == s + 4, "strchr");
    CHECK(sam_strchr(s, 0) == s + 11, "strchr NUL");
    CHECK(sam_strchr(s, 'z') == NULL, "strchr miss");
    CHECK(sam_strrchr(s, 'o') == s + 7, "strrchr");
    CHECK(sam_strstr(s, "wor") == s + 6, "strstr");
    CHECK(sam_strstr(s, "") == s, "strstr empty");
    CHECK(sam_strstr(s, "worlds") == NULL, "strstr miss");
    char buf[32] = "ab";
    sam_strcat(buf, "cd");
    CHECK(strcmp(buf, "abcd") == 0, "strcat");
    sam_strncat(buf, "efgh", 2);
    CHECK(strcmp(buf, "abcdef") == 0, "strncat");
    CHECK(sam_strcmp("abc", "abd") < 0 && sam_strcmp("b", "a") > 0 && sam_strcmp("x", "x") == 0, "strcmp");

    // strtok restarts on a new string even if the last one was not
    // fully consumed.
    char t1[] = "a b c";
    char t2[] = "x y";
    sam_strtok(t1, " ");
    char* first = sam_strtok(t2, " ");
    CHECK(first && strcmp(first, "x") == 0, "strtok restart got \"%s\"", first ? first : "(null)");
}

static int count_args(struct command_argument* a, const char** want, int n){
    int i = 0;
    for(; a; a = a->next, i++){
        if(i >= n || strcmp(a->argument, want[i]) != 0){
            return 0;
        }
    }
    return i == n;
}

#define PARSE(line, ...) do { const char* w_[] = {__VA_ARGS__}; \
    struct command_argument* c_ = sam_samos_parse_command(line, 1024); \
    CHECK(count_args(c_, w_, sizeof w_ / sizeof w_[0]), "parse %s", line); \
    sam_samos_free_command(c_); } while(0)

static void test_parser(void){
    int before = allocs_live;
    PARSE("echo a b", "echo", "a", "b");
    PARSE("  echo   spaced\tout  ", "echo", "spaced", "out");
    PARSE("echo \"a b\"", "echo", "a b");
    PARSE("echo 'a  b' c", "echo", "a  b", "c");
    PARSE("echo \"say \\\"hi\\\"\"", "echo", "say \"hi\"");
    PARSE("echo a\\ b", "echo", "a b");
    PARSE("echo \"a\"'b'c", "echo", "abc");
    PARSE("echo !@#$%^&*()_+{}|:<>?~", "echo", "!@#$%^&*()_+{}|:<>?~");
    CHECK(sam_samos_parse_command("", 1024) == NULL, "empty line -> NULL");
    CHECK(sam_samos_parse_command("   ", 1024) == NULL, "blank line -> NULL");
    CHECK(sam_samos_parse_command("echo \"open", 1024) == NULL, "unterminated quote -> NULL");
    CHECK(allocs_live == before, "parser leaks: %d live blocks", allocs_live - before);

    // samos_system_run frees its list (the per-token leak).
    before = allocs_live;
    for(int i = 0; i < 100; i++){
        sam_samos_system_run("prog a b c d");
    }
    CHECK(allocs_live == before, "system_run leaks %d blocks", allocs_live - before);
}

static void test_readline(void){
    char out[64];
    // Backspace at column 0 must not store 0x08 or echo it.
    console_len = 0; console[0] = 0;
    keys = "\b\bab\bc\r";
    sam_samos_terminal_readline(out, sizeof out, 1);
    CHECK(strcmp(out, "ac") == 0, "readline backspace: \"%s\"", out);
    CHECK(strcmp(console, "ab\bc") == 0, "readline echo: %zu bytes", console_len);

    // Arrow keys / Delete arrive as escape sequences and are skipped.
    keys = "x\x1b[Ay\x1b[3~z\r";
    sam_samos_terminal_readline(out, sizeof out, 0);
    CHECK(strcmp(out, "xyz") == 0, "readline escapes: \"%s\"", out);

    // DEL (0x7F) also erases; max length is honoured.
    keys = "abc\x7f\r";
    sam_samos_terminal_readline(out, sizeof out, 0);
    CHECK(strcmp(out, "ab") == 0, "readline DEL");
    keys = "abcdefgh";
    sam_samos_terminal_readline(out, 4, 0);
    CHECK(strcmp(out, "abc") == 0, "readline max");
    keys = "";
}

int main(void){
    test_printf();
    test_numbers();
    test_strings();
    test_parser();
    test_readline();
    printf("libc host test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
