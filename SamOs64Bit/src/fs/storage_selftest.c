// Storage self-test. See storage_selftest.h.

#include "storage_selftest.h"
#include "fs/file.h"
#include "fs/fat/fat.h"
#include "disk/disk.h"
#include "disk/driver.h"
#include "string/string.h"
#include "memory/memory.h"
#include "memory/heap/kheap.h"
#include "status.h"
#include "kernel.h"
#include <stdint.h>
#include <stdbool.h>

#define ST_LOG_SIZE   16384
#define ST_BIG_SIZE   300000
#define ST_TRUNC_SIZE 10000
#define ST_MANY_FILES 40
#define ST_LINUX_BIG  70000

struct st_ctx {
    int   drive;
    char* log;
    int   log_len;
    int   pass;
    int   fail;
};

// Same formula as tests64/storage/pattern.py.
static uint8_t st_pattern(uint32_t i, uint32_t seed){
    return (uint8_t)((i * 31u + seed * 17u + (i >> 8)) & 0xFF);
}

static void st_append(struct st_ctx* c, const char* s){
    while(*s && c->log_len < ST_LOG_SIZE - 1){
        c->log[c->log_len++] = *s++;
    }
    c->log[c->log_len] = 0;
}

static void st_append_num(struct st_ctx* c, int64_t v){
    char buf[24];
    int n = 0;
    bool neg = v < 0;
    uint64_t u = neg ? (uint64_t)(-v) : (uint64_t)v;
    do {
        buf[n++] = (char)('0' + u % 10);
        u /= 10;
    } while(u);
    if(neg){
        buf[n++] = '-';
    }
    char out[24];
    for(int i = 0; i < n; i++){
        out[i] = buf[n - 1 - i];
    }
    out[n] = 0;
    st_append(c, out);
}

static void st_check(struct st_ctx* c, bool ok, const char* name, int64_t detail){
    st_append(c, ok ? "PASS " : "FAIL ");
    st_append(c, name);
    if(!ok){
        st_append(c, " (");
        st_append_num(c, detail);
        st_append(c, ")");
        print("storage: FAIL ");
        print(name);
        print("\n");
    }
    st_append(c, "\n");
    if(ok){
        c->pass++;
    } else {
        c->fail++;
    }
}

// "N:" + rest into a static buffer (one path in use at a time).
static const char* st_path(struct st_ctx* c, const char* rest){
    static char buf[SAMOS_MAX_PATH];
    buf[0] = (char)('0' + c->drive);
    buf[1] = ':';
    strncpy(buf + 2, rest, sizeof(buf) - 3);
    buf[sizeof(buf) - 1] = 0;
    return buf;
}

static int st_write_file(struct st_ctx* c, const char* rest, const char* mode,
                         const void* data, uint32_t len){
    int fd = fopen(st_path(c, rest), mode);
    if(fd <= 0){
        return -EIO;
    }
    int res = len ? fwrite(data, len, 1, fd) : 1;
    fclose(fd);
    return res == 1 ? 0 : (res < 0 ? res : -EIO);
}

// Read a whole file (up to max bytes). Returns its size or < 0.
static int st_read_file(struct st_ctx* c, const char* rest, char* out, uint32_t max){
    int fd = fopen(st_path(c, rest), "r");
    if(fd <= 0){
        return -ENOENT;
    }
    struct file_stat st;
    int res = fstat(fd, &st);
    if(res == 0){
        uint32_t n = st.filesize < max ? st.filesize : max;
        res = n ? fread(out, n, 1, fd) : 1;
        res = res == 1 ? (int)st.filesize : -EIO;
    }
    fclose(fd);
    return res;
}

static bool st_text_equal(struct st_ctx* c, const char* rest, const char* expect){
    char buf[256];
    int n = st_read_file(c, rest, buf, sizeof(buf) - 1);
    if(n < 0 || n != strlen(expect)){
        return false;
    }
    buf[n] = 0;
    return strncmp(buf, expect, n + 1) == 0;
}

static bool st_pattern_file_ok(struct st_ctx* c, const char* rest, uint32_t size, uint32_t seed,
                               uint32_t chunk){
    int fd = fopen(st_path(c, rest), "r");
    if(fd <= 0){
        return false;
    }
    bool ok = true;
    struct file_stat st;
    if(fstat(fd, &st) < 0 || st.filesize != size){
        ok = false;
    }
    char* buf = kzalloc(chunk);
    uint32_t pos = 0;
    while(ok && buf && pos < size){
        uint32_t n = size - pos < chunk ? size - pos : chunk;
        if(fread(buf, 1, n, fd) != (int)n){
            ok = false;
            break;
        }
        for(uint32_t i = 0; i < n; i++){
            if((uint8_t)buf[i] != st_pattern(pos + i, seed)){
                ok = false;
                break;
            }
        }
        pos += n;
    }
    // At end of file a read returns 0 items.
    if(ok && buf && fread(buf, 1, 1, fd) != 0){
        ok = false;
    }
    kfree(buf);
    fclose(fd);
    return ok && buf;
}

static int st_write_pattern(struct st_ctx* c, const char* rest, uint32_t size, uint32_t seed){
    int fd = fopen(st_path(c, rest), "w");
    if(fd <= 0){
        return -EIO;
    }
    // Uneven chunk sizes cross sector and cluster boundaries.
    static const uint32_t chunks[] = { 1, 511, 513, 4097, 1000, 65536, 3 };
    char* buf = kzalloc(65536);
    uint32_t pos = 0;
    int k = 0;
    int res = buf ? 0 : -ENOMEM;
    while(res == 0 && pos < size){
        uint32_t n = chunks[k++ % 7];
        if(n > size - pos){
            n = size - pos;
        }
        for(uint32_t i = 0; i < n; i++){
            buf[i] = (char)st_pattern(pos + i, seed);
        }
        if(fwrite(buf, 1, n, fd) != (int)n){
            res = -EIO;
        }
        pos += n;
    }
    kfree(buf);
    fclose(fd);
    return res;
}

static int st_count_dir(struct st_ctx* c, const char* rest, const char* must_have, bool* found){
    int fd = fopendir(st_path(c, rest));
    if(fd <= 0){
        return fd < 0 ? fd : -EIO;
    }
    struct file_dirent* e = kzalloc(sizeof(*e));
    int count = 0;
    if(found){
        *found = false;
    }
    while(e && freaddir(fd, e) == 1){
        count++;
        if(must_have && found && strncmp(e->name, must_have, FILE_DIRENT_NAME_MAX) == 0){
            *found = true;
        }
    }
    kfree(e);
    fclose(fd);
    return count;
}

static void st_many_name(char* out, int n){
    // "/SUBDIR/MANY/file number NN with a long name.txt"
    const char* a = "/SUBDIR/MANY/file number ";
    const char* b = " with a long name.txt";
    int o = 0;
    for(const char* p = a; *p; p++) out[o++] = *p;
    out[o++] = (char)('0' + n / 10);
    out[o++] = (char)('0' + n % 10);
    for(const char* p = b; *p; p++) out[o++] = *p;
    out[o] = 0;
}

static void st_many_text(char* out, int n){
    const char* a = "file ";
    int o = 0;
    for(const char* p = a; *p; p++) out[o++] = *p;
    out[o++] = (char)('0' + n / 10);
    out[o++] = (char)('0' + n % 10);
    out[o++] = '\n';
    out[o] = 0;
}

static const char* ST_BIG_PATH = "/SUBDIR/Nested Long Directory Name/Big File Data.bin";
static const char* ST_LINUX_TEXT = "written by mtools on linux\n";

// Checks shared by both phases: files that must exist after the
// WRITE phase, whether in the same boot or after a reboot.
static void st_verify_written(struct st_ctx* c){
    st_check(c, st_pattern_file_ok(c, ST_BIG_PATH, ST_BIG_SIZE, 1, 777), "big file content", 0);
    st_check(c, st_text_equal(c, "/hello.txt", "Hello from SamOS\n"), "hello.txt content", 0);
    st_check(c, st_text_equal(c, "/MixedCase.Txt", "mixed case long name\n"), "LFN mixed case file", 0);
    st_check(c, st_text_equal(c, "/append.log", "one\ntwo\n"), "append content", 0);
    st_check(c, st_pattern_file_ok(c, "/TRUNC.BIN", 3000, 2, 1000), "truncated content", 0);

    {
        char* buf = kzalloc(5003);
        int n = buf ? st_read_file(c, "/SPARSE.BIN", buf, 5003) : -ENOMEM;
        bool ok = n == 5003 && buf[5000] == 'E' && buf[5001] == 'N' && buf[5002] == 'D';
        for(int i = 0; ok && i < 5000; i++){
            if(buf[i] != 0){
                ok = false;
            }
        }
        st_check(c, ok, "sparse file zero gap", n);
        kfree(buf);
    }

    st_check(c, fopen(st_path(c, "/DELETE ME.txt"), "r") == 0, "deleted file stays gone", 0);

    bool found = false;
    int n = st_count_dir(c, "/SUBDIR/MANY", "file number 07 with a long name.txt", &found);
    st_check(c, n == ST_MANY_FILES && found, "readdir of grown directory", n);
    bool many_ok = true;
    for(int i = 0; i < ST_MANY_FILES; i += 13){
        char name[64], text[16];
        st_many_name(name, i);
        st_many_text(text, i);
        if(!st_text_equal(c, name, text)){
            many_ok = false;
        }
    }
    st_check(c, many_ok, "files in grown directory", 0);
}

static void st_phase_write(struct st_ctx* c){
    int res;

    res = fmkdir(st_path(c, "/SUBDIR"));
    st_check(c, res == 0, "mkdir /SUBDIR", res);
    res = fmkdir(st_path(c, "/SUBDIR"));
    st_check(c, res == -EEXIST, "mkdir existing fails", res);
    res = fmkdir(st_path(c, "/SUBDIR/Nested Long Directory Name"));
    st_check(c, res == 0, "mkdir long-name subdirectory", res);
    res = fmkdir(st_path(c, "/NOPE/child"));
    st_check(c, res < 0, "mkdir with missing parent fails", res);

    res = st_write_pattern(c, ST_BIG_PATH, ST_BIG_SIZE, 1);
    st_check(c, res == 0, "write big file", res);

    res = st_write_file(c, "/hello.txt", "w", "Hello from SamOS\n", 17);
    st_check(c, res == 0, "write hello.txt", res);
    res = st_write_file(c, "/MixedCase.Txt", "w", "mixed case long name\n", 21);
    st_check(c, res == 0, "write MixedCase.Txt", res);

    // Seeking.
    {
        int fd = fopen(st_path(c, "/hello.txt"), "r");
        char buf[8] = {0};
        st_check(c, fd > 0, "open hello.txt", fd);
        st_check(c, fseek(fd, 0, SEEK_END) == 0, "SEEK_END 0", 0);
        st_check(c, fread(buf, 1, 1, fd) == 0, "read at EOF returns 0", 0);
        st_check(c, fseek(fd, -6, SEEK_END) == 0 && fread(buf, 1, 6, fd) == 6 &&
                    strncmp(buf, "SamOS\n", 6) == 0, "SEEK_END -6 then read", 0);
        st_check(c, fseek(fd, 100, SEEK_SET) == 0 && fread(buf, 1, 1, fd) == 0,
                 "seek past end then read", 0);
        st_check(c, fseek(fd, -1000, SEEK_CUR) < 0, "seek before start fails", 0);
        st_check(c, fseek(fd, 6, SEEK_SET) == 0 && fread(buf, 1, 4, fd) == 4 &&
                    fread(buf + 4, 1, 3, fd) == 3 && strncmp(buf, "from Sa", 7) == 0,
                 "sequential reads advance", 0);
        st_check(c, fwrite("x", 1, 1, fd) < 0, "write to read-mode file fails", 0);
        fclose(fd);
    }

    // Sparse write.
    {
        int fd = fopen(st_path(c, "/SPARSE.BIN"), "w");
        bool ok = fd > 0 && fseek(fd, 5000, SEEK_SET) == 0 && fwrite("END", 3, 1, fd) == 1;
        if(fd > 0){
            fclose(fd);
        }
        st_check(c, ok, "write after seek past end", 0);
    }

    // Append.
    res = st_write_file(c, "/append.log", "a", "one\n", 4);
    if(res == 0){
        res = st_write_file(c, "/append.log", "a", "two\n", 4);
    }
    st_check(c, res == 0, "append twice", res);

    // Truncate down then up.
    res = st_write_pattern(c, "/TRUNC.BIN", ST_TRUNC_SIZE, 2);
    {
        int fd = res == 0 ? fopen(st_path(c, "/TRUNC.BIN"), "a") : 0;
        bool ok = fd > 0 && ftruncate(fd, 3000) == 0;
        struct file_stat st;
        ok = ok && fstat(fd, &st) == 0 && st.filesize == 3000;
        if(fd > 0){
            fclose(fd);
        }
        st_check(c, ok, "ftruncate shrink", res);
        ok = st_pattern_file_ok(c, "/TRUNC.BIN", 3000, 2, 1000);
        st_check(c, ok, "content after shrink", 0);
        // Grow a copy and check the zero tail.
        res = st_write_pattern(c, "/TRUNC2.BIN", 3000, 2);
        fd = res == 0 ? fopen(st_path(c, "/TRUNC2.BIN"), "a") : 0;
        ok = fd > 0 && ftruncate(fd, 6000) == 0;
        if(fd > 0){
            fclose(fd);
        }
        char* buf = kzalloc(6000);
        ok = ok && buf && st_read_file(c, "/TRUNC2.BIN", buf, 6000) == 6000;
        for(int i = 3000; ok && i < 6000; i++){
            if(buf[i] != 0){
                ok = false;
            }
        }
        kfree(buf);
        st_check(c, ok, "ftruncate grow zero-fills", 0);
    }

    // "w" on an existing file truncates it.
    res = st_write_file(c, "/TRUNC2.BIN", "w", "short\n", 6);
    st_check(c, res == 0 && st_text_equal(c, "/TRUNC2.BIN", "short\n"), "w truncates", res);

    // Delete.
    res = st_write_file(c, "/DELETE ME.txt", "w", "bye\n", 4);
    st_check(c, res == 0, "create file to delete", res);
    res = fremove(st_path(c, "/DELETE ME.txt"));
    st_check(c, res == 0, "remove file", res);
    st_check(c, fopen(st_path(c, "/DELETE ME.txt"), "r") == 0, "removed file is gone", 0);
    res = fremove(st_path(c, "/SUBDIR"));
    st_check(c, res == -ENOTEMPTY, "remove non-empty dir fails", res);
    res = fmkdir(st_path(c, "/EMPTYDIR"));
    if(res == 0){
        res = fremove(st_path(c, "/EMPTYDIR"));
    }
    st_check(c, res == 0, "mkdir + remove empty dir", res);

    // A directory that grows past one cluster.
    res = fmkdir(st_path(c, "/SUBDIR/MANY"));
    st_check(c, res == 0, "mkdir /SUBDIR/MANY", res);
    bool ok = true;
    for(int i = 0; i < ST_MANY_FILES; i++){
        char name[64], text[16];
        st_many_name(name, i);
        st_many_text(text, i);
        if(st_write_file(c, name, "w", text, strlen(text)) < 0){
            ok = false;
        }
    }
    st_check(c, ok, "create many long-named files", 0);

    // Root listing contains our files and the Linux directory.
    bool found = false;
    int n = st_count_dir(c, "/", "MixedCase.Txt", &found);
    st_check(c, n > 0 && found, "readdir root finds LFN name", n);
    n = st_count_dir(c, "/", "hello.txt", &found);
    st_check(c, found, "readdir root finds lowercase 8.3 name", n);

    // Files Linux (mtools) wrote before this boot.
    st_check(c, st_text_equal(c, "/FROMLNX/Linux Long Name File.txt", ST_LINUX_TEXT),
             "read Linux LFN file", 0);
    st_check(c, st_text_equal(c, "/fromlnx/linux long name file.TXT", ST_LINUX_TEXT),
             "case-insensitive lookup", 0);
    st_check(c, st_pattern_file_ok(c, "/FROMLNX/linuxbig.bin", ST_LINUX_BIG, 3, 4096),
             "read Linux big file", 0);

    st_verify_written(c);
}

static void st_phase_verify(struct st_ctx* c){
    st_verify_written(c);
    st_check(c, st_pattern_file_ok(c, "/FROMLNX/linuxbig.bin", ST_LINUX_BIG, 3, 4096),
             "Linux big file after reboot", 0);
    int res = st_write_file(c, "/SUBDIR/AFTERBOOT.TXT", "w", "second boot\n", 12);
    st_check(c, res == 0, "write after reboot", res);
}

static void st_run_disk(struct disk* disk){
    struct st_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.drive = disk->id;
    ctx.log   = kzalloc(ST_LOG_SIZE);
    if(!ctx.log || disk->id > 9){
        print("storage: cannot test disk (log alloc or id > 9)\n");
        return;
    }

    char cmd[16] = {0};
    int n = st_read_file(&ctx, "/TESTCMD.TXT", cmd, sizeof(cmd) - 1);
    if(n < 0){
        print("storage: SMTEST disk without TESTCMD.TXT\n");
        return;
    }
    bool write_phase = strncmp(cmd, "WRITE", 5) == 0;
    bool verify_phase = strncmp(cmd, "VERIFY", 6) == 0;
    if(!write_phase && !verify_phase){
        return;
    }

    st_append(&ctx, "INFO fat=");
    st_append_num(&ctx, fat_type(disk));
    st_append(&ctx, " driver=");
    st_append(&ctx, disk->hardware_disk->driver ? disk->hardware_disk->driver->name : "?");
    st_append(&ctx, " start_lba=");
    st_append_num(&ctx, (int64_t)disk->starting_lba);
    st_append(&ctx, " sectors=");
    st_append_num(&ctx, (int64_t)disk->total_sectors);
    st_append(&ctx, "\n");
    st_check(&ctx, fat_check_sizes(), "FAT struct sizes", 0);

    if(write_phase){
        st_phase_write(&ctx);
    } else {
        st_phase_verify(&ctx);
    }

    st_append(&ctx, "DONE pass=");
    st_append_num(&ctx, ctx.pass);
    st_append(&ctx, " fail=");
    st_append_num(&ctx, ctx.fail);
    st_append(&ctx, "\n");
    int res = st_write_file(&ctx, write_phase ? "/RESULT1.TXT" : "/RESULT2.TXT", "w",
                            ctx.log, ctx.log_len);
    print(ctx.fail == 0 && res == 0 ? "storage: selftest PASS\n" : "storage: selftest FAIL\n");
    kfree(ctx.log);
}

static void st_print_disks(void){
    for(int i = 0; i < disk_count(); i++){
        struct disk* d = disk_get(i);
        if(!d){
            continue;
        }
        char label[12] = {0};
        if(d->filesystem && d->filesystem->volume_name){
            d->filesystem->volume_name(d->fs_private, label, sizeof(label));
        }
        print("disk ");
        print(itoa(d->id));
        print(d->type == SAMOS_DISK_TYPE_REAL ? " real " : " part ");
        print(d->hardware_disk && d->hardware_disk->driver ? d->hardware_disk->driver->name : "?");
        print(" fs=");
        print(d->filesystem ? d->filesystem->name : "-");
        print(" label=");
        print(label);
        print(d->read_only ? " ro\n" : " rw\n");
    }
}

int storage_selftest(void){
    st_print_disks();
    for(int i = 0; i < disk_count(); i++){
        struct disk* d = disk_get(i);
        if(!d || !d->filesystem || !d->filesystem->volume_name){
            continue;
        }
        char label[12] = {0};
        d->filesystem->volume_name(d->fs_private, label, sizeof(label));
        if(strncmp(label, STORAGE_SELFTEST_LABEL, 11) == 0){
            st_run_disk(d);
        }
    }
    return 0;
}
