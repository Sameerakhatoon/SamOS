// FAT12/16/32 driver with long file names and writes.
//
// This replaces the course's fat16.c, which was FAT16-only,
// read-only and root-directory-only:
//   - fat16_new_fat_item_for_directory_item overwrote every
//     item's type with FILE, so subdirectories never worked;
//   - fat16_read never advanced the descriptor position and did
//     not clamp to the file size;
//   - SEEK_END was unimplemented and seeking to the end failed;
//   - LFN and deleted entries were counted as items, so lookups
//     past them read the wrong entry;
//   - the cluster high bits were OR-ed in without a shift.
//
// Layout of a FAT volume (all in sectors of bytes_per_sector):
//   [reserved (boot sector, FSInfo...)] [FAT 1] ... [FAT n]
//   [fixed root directory, FAT12/16 only] [data clusters 2...]
//
// Directories are addressed by their first cluster. Cluster 0
// means the fixed root directory on FAT12/16; on FAT32 the root
// is an ordinary cluster chain starting at root_cluster (a ".."
// entry that points at the root still stores 0).
//
// Every write goes straight to the disk (the streamer cache is
// write-through), so the volume is consistent after each call.

#include "fat.h"
#include "string/string.h"
#include "status.h"
#include "config.h"
#include "disk/disk.h"
#include "disk/streamer.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"
#include "kernel.h"
#include <stdint.h>
#include <stdbool.h>

#define FAT_ATTR_READ_ONLY   0x01
#define FAT_ATTR_HIDDEN      0x02
#define FAT_ATTR_SYSTEM      0x04
#define FAT_ATTR_VOLUME_ID   0x08
#define FAT_ATTR_DIRECTORY   0x10
#define FAT_ATTR_ARCHIVE     0x20
#define FAT_ATTR_LFN         0x0F
#define FAT_ATTR_LFN_MASK    0x3F

#define FAT_ENTRY_END        0x00
#define FAT_ENTRY_DELETED    0xE5
#define FAT_ENTRY_KANJI_E5   0x05   // first byte 0x05 stands for 0xE5

// NT case flags (directory entry byte 12): Windows and Linux
// store all-lowercase 8.3 names this way instead of with LFNs.
#define FAT_NT_LOWER_BASE    0x08
#define FAT_NT_LOWER_EXT     0x10

#define FAT_LFN_LAST         0x40
#define FAT_LFN_CHARS        13
#define FAT_LFN_MAX_CHARS    255
#define FAT_LFN_MAX_ENTRIES  20
// UTF-8 needs up to 3 bytes per UCS-2 character.
#define FAT_NAME_BUF         (FAT_LFN_MAX_CHARS * 3 + 1)

#define FAT_MAX_DIR_ENTRIES  65536
#define FAT_MAX_FILE_SIZE    0xFFFFFFFFull
#define FAT_UNKNOWN          0xFFFFFFFFu

#define FAT_FSINFO_LEAD_SIG   0x41615252u
#define FAT_FSINFO_STRUCT_SIG 0x61417272u

struct fat_bpb {
    uint8_t  jmp[3];
    uint8_t  oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t root_entries;
    uint16_t total_sectors16;
    uint8_t  media;
    uint16_t fat_size16;
    uint16_t sectors_per_track;
    uint16_t heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors32;
} __attribute__((packed));

struct fat_ebpb16 {
    uint8_t  drive_number;
    uint8_t  reserved;
    uint8_t  boot_signature;     // 0x29: the next three fields exist
    uint32_t volume_id;
    uint8_t  label[11];
    uint8_t  fs_type[8];
} __attribute__((packed));

struct fat_ebpb32 {
    uint32_t fat_size32;
    uint16_t ext_flags;          // bit 7: mirroring off, bits 0-3 active FAT
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fsinfo_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    uint8_t  label[11];
    uint8_t  fs_type[8];
} __attribute__((packed));

struct fat_directory_item {
    uint8_t  filename[8];
    uint8_t  ext[3];
    uint8_t  attribute;
    uint8_t  nt_case;
    uint8_t  creation_time_tenths_of_a_sec;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access;
    uint16_t high_16_bits_first_cluster;
    uint16_t last_mod_time;
    uint16_t last_mod_date;
    uint16_t low_16_bits_first_cluster;
    uint32_t filesize;
} __attribute__((packed));

struct fat_lfn_entry {
    uint8_t  seq;
    uint16_t name1[5];
    uint8_t  attribute;          // always 0x0F
    uint8_t  type;
    uint8_t  checksum;           // of the 8.3 name it belongs to
    uint16_t name2[6];
    uint16_t first_cluster;      // always 0
    uint16_t name3[2];
} __attribute__((packed));

struct fat_fs {
    struct disk*        disk;
    struct disk_stream* stream;
    int                 type;            // 12, 16 or 32
    uint32_t            bps;             // bytes per sector
    uint32_t            spc;             // sectors per cluster
    uint32_t            cluster_bytes;
    uint32_t            reserved;
    uint32_t            fat_count;
    uint32_t            fat_sectors;
    int                 active_fat;      // -1: all FATs mirrored
    uint32_t            root_entries;    // FAT12/16 fixed root
    uint32_t            root_dir_sector;
    uint32_t            root_dir_sectors;
    uint32_t            first_data_sector;
    uint32_t            cluster_count;   // valid clusters: 2 .. cluster_count+1
    uint32_t            root_id;         // 0 (fixed root) or FAT32 root cluster
    uint32_t            eoc_min;         // first end-of-chain value
    uint32_t            eoc;             // value we write for end of chain
    uint32_t            fsinfo_sector;   // 0 = none
    uint32_t            free_count;      // FAT_UNKNOWN until counted
    uint32_t            next_free;
    bool                fsinfo_dirty;
    char                label[12];
};

// Position inside a directory's cluster chain (cached so that
// sequential entry access does not rewalk the chain).
struct fat_dir_cursor {
    uint32_t first;
    uint32_t cidx;
    uint32_t cluster;
};

struct fat_scan {
    struct fat_dir_cursor cur;
    uint32_t index;
    bool     lfn_valid;
    int      lfn_next;
    uint8_t  lfn_sum;
    uint32_t lfn_start;
    uint16_t lfn[FAT_LFN_MAX_ENTRIES * FAT_LFN_CHARS + 1];
};

struct fat_entry_info {
    struct fat_directory_item item;
    uint32_t index;          // index of the 8.3 entry in its directory
    uint32_t first_index;    // index of its first LFN entry (or == index)
    char     name[FAT_NAME_BUF];
    char     short_name[13];
};

struct fat_file {
    struct fat_fs*   fs;
    bool             is_dir;
    bool             is_root;
    uint32_t         parent;         // directory holding the entry
    uint32_t         index;          // 8.3 entry index in parent
    uint32_t         first_cluster;
    uint32_t         size;
    uint32_t         pos;
    uint8_t          attr;
    FILE_MODE        mode;
    uint32_t         cache_cidx;
    uint32_t         cache_cluster;  // 0 = no cached position
    struct fat_scan* scan;           // directories opened for readdir
};

// Fallback timestamp, used when no time source is set or it fails.
static uint16_t fat_clock_date = ((2026 - 1980) << 9) | (10 << 5) | 16;
static uint16_t fat_clock_time = (12 << 11);
static int64_t (*fat_time_source)(void);

static int   fat_resolve(struct disk* disk);
static void* fat_open(struct disk* disk, struct path_part* path, FILE_MODE mode);
static int   fat_read(struct disk* disk, void* private, uint32_t size, uint32_t nmemb, char* out);
static int   fat_write(struct disk* disk, void* private, uint32_t size, uint32_t nmemb, const char* in);
static int   fat_seek(void* private, int64_t offset, FILE_SEEK_MODE seek_mode);
static int   fat_stat(struct disk* disk, void* private, struct file_stat* stat);
static int   fat_close(void* private);
static int   fat_truncate(void* private, uint32_t size);
static int   fat_unlink(struct disk* disk, struct path_part* path);
static int   fat_mkdir(struct disk* disk, struct path_part* path);
static void* fat_opendir(struct disk* disk, struct path_part* path);
static int   fat_readdir(void* private, struct file_dirent* entry);
static int   fat_volume_name(void* private, char* name_out, size_t max);

static struct filesystem fat_fs = {
    .resolve     = fat_resolve,
    .open        = fat_open,
    .read        = fat_read,
    .seek        = fat_seek,
    .stat        = fat_stat,
    .close       = fat_close,
    .volume_name = fat_volume_name,
    .write       = fat_write,
    .truncate    = fat_truncate,
    .unlink      = fat_unlink,
    .mkdir       = fat_mkdir,
    .opendir     = fat_opendir,
    .readdir     = fat_readdir,
};

struct filesystem* fat_init(void){
    strcpy(fat_fs.name, "FAT");
    return &fat_fs;
}

void fat_set_clock(uint16_t dos_date, uint16_t dos_time){
    fat_clock_date = dos_date;
    fat_clock_time = dos_time;
}

void fat_set_time_source(int64_t (*now)(void)){
    fat_time_source = now;
}

// The time now in DOS format. FAT stores local time with
// 2-second resolution and no zone; SamOS takes the clock's time as
// local. Years outside DOS's 1980-2107 fall back to the fixed clock.
static void fat_now(uint16_t* dos_date, uint16_t* dos_time){
    *dos_date = fat_clock_date;
    *dos_time = fat_clock_time;
    int64_t t = fat_time_source ? fat_time_source() : -1;
    if(t < 0){
        return;
    }
    int64_t days = t / 86400;
    int64_t secs = t % 86400;
    // Civil date of a day count since 1970-01-01 (Howard Hinnant's
    // civil_from_days, the inverse of rtc_to_unix).
    int64_t z   = days + 719468;
    int64_t era = z / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp  = (5 * doy + 2) / 153;
    int64_t day = doy - (153 * mp + 2) / 5 + 1;
    int64_t mon = mp < 10 ? mp + 3 : mp - 9;
    int64_t yr  = yoe + era * 400 + (mon <= 2);
    if(yr < 1980 || yr > 2107){
        return;
    }
    *dos_date = (uint16_t)(((yr - 1980) << 9) | (mon << 5) | day);
    *dos_time = (uint16_t)(((secs / 3600) << 11) | (((secs / 60) % 60) << 5) | ((secs % 60) / 2));
}

int fat_check_sizes(void){
    return sizeof(struct fat_bpb) == 36 &&
           sizeof(struct fat_ebpb16) == 26 &&
           sizeof(struct fat_ebpb32) == 54 &&
           sizeof(struct fat_directory_item) == 32 &&
           sizeof(struct fat_lfn_entry) == 32;
}

int fat_type(struct disk* disk){
    if(!disk || disk->filesystem != &fat_fs || !disk->fs_private){
        return -1;
    }
    return ((struct fat_fs*)disk->fs_private)->type;
}

// ---------------------------------------------------------------
// Low-level access
// ---------------------------------------------------------------

static int fat_rw(struct fat_fs* fs, uint64_t off, void* buf, int len, bool write){
    int res = diskstreamer_seek(fs->stream, (int64_t)off);
    if(res < 0){
        return res;
    }
    return write ? diskstreamer_write(fs->stream, buf, len)
                 : diskstreamer_read(fs->stream, buf, len);
}

static uint64_t fat_cluster_offset(struct fat_fs* fs, uint32_t cluster){
    return ((uint64_t)fs->first_data_sector + (uint64_t)(cluster - 2) * fs->spc) * fs->bps;
}

static bool fat_cluster_valid(struct fat_fs* fs, uint32_t cluster){
    return cluster >= 2 && cluster < fs->cluster_count + 2;
}

static bool fat_is_eoc(struct fat_fs* fs, uint32_t value){
    return value >= fs->eoc_min;
}

static uint64_t fat_table_offset(struct fat_fs* fs, uint32_t fat){
    return ((uint64_t)fs->reserved + (uint64_t)fat * fs->fat_sectors) * fs->bps;
}

static int fat_get(struct fat_fs* fs, uint32_t cluster, uint32_t* out){
    uint64_t base = fat_table_offset(fs, fs->active_fat >= 0 ? (uint32_t)fs->active_fat : 0);
    int res;
    if(fs->type == 12){
        uint8_t b[2];
        res = fat_rw(fs, base + cluster + cluster / 2, b, 2, false);
        uint32_t v = b[0] | ((uint32_t)b[1] << 8);
        *out = (cluster & 1) ? (v >> 4) : (v & 0xFFF);
    } else if(fs->type == 16){
        uint16_t v = 0;
        res = fat_rw(fs, base + (uint64_t)cluster * 2, &v, 2, false);
        *out = v;
    } else {
        uint32_t v = 0;
        res = fat_rw(fs, base + (uint64_t)cluster * 4, &v, 4, false);
        *out = v & 0x0FFFFFFF;
    }
    return res;
}

static int fat_set(struct fat_fs* fs, uint32_t cluster, uint32_t value){
    for(uint32_t fat = 0; fat < fs->fat_count; fat++){
        if(fs->active_fat >= 0 && fat != (uint32_t)fs->active_fat){
            continue;
        }
        uint64_t base = fat_table_offset(fs, fat);
        int res;
        if(fs->type == 12){
            uint8_t b[2];
            uint64_t off = base + cluster + cluster / 2;
            res = fat_rw(fs, off, b, 2, false);
            if(res < 0){
                return res;
            }
            uint16_t w = b[0] | ((uint16_t)b[1] << 8);
            if(cluster & 1){
                w = (w & 0x000F) | (uint16_t)((value & 0xFFF) << 4);
            } else {
                w = (w & 0xF000) | (uint16_t)(value & 0xFFF);
            }
            b[0] = (uint8_t)w;
            b[1] = (uint8_t)(w >> 8);
            res = fat_rw(fs, off, b, 2, true);
        } else if(fs->type == 16){
            uint16_t v = (uint16_t)value;
            res = fat_rw(fs, base + (uint64_t)cluster * 2, &v, 2, true);
        } else {
            uint32_t old = 0;
            uint64_t off = base + (uint64_t)cluster * 4;
            res = fat_rw(fs, off, &old, 4, false);
            if(res < 0){
                return res;
            }
            // The top 4 bits are reserved and must be preserved.
            uint32_t v = (old & 0xF0000000u) | (value & 0x0FFFFFFFu);
            res = fat_rw(fs, off, &v, 4, true);
        }
        if(res < 0){
            return res;
        }
    }
    return 0;
}

static int fat_zero_cluster(struct fat_fs* fs, uint32_t cluster){
    char* zero = kzalloc(fs->cluster_bytes);
    if(!zero){
        return -ENOMEM;
    }
    int res = fat_rw(fs, fat_cluster_offset(fs, cluster), zero, fs->cluster_bytes, true);
    kfree(zero);
    return res;
}

static int fat_count_free(struct fat_fs* fs){
    uint32_t free = 0;
    for(uint32_t c = 2; c < fs->cluster_count + 2; c++){
        uint32_t v;
        int res = fat_get(fs, c, &v);
        if(res < 0){
            return res;
        }
        if(v == 0){
            free++;
        }
    }
    fs->free_count = free;
    return 0;
}

// Allocate one cluster, mark it end-of-chain and link it after
// `prev` (if prev != 0).
static int fat_alloc_cluster(struct fat_fs* fs, uint32_t prev, bool zero, uint32_t* out){
    if(fs->disk->read_only){
        return -ERDONLY;
    }
    if(fs->type == 32 && fs->free_count == FAT_UNKNOWN){
        int res = fat_count_free(fs);
        if(res < 0){
            return res;
        }
    }
    uint32_t start = fat_cluster_valid(fs, fs->next_free) ? fs->next_free : 2;
    for(uint32_t i = 0; i < fs->cluster_count; i++){
        uint32_t c = 2 + ((start - 2 + i) % fs->cluster_count);
        uint32_t v;
        int res = fat_get(fs, c, &v);
        if(res < 0){
            return res;
        }
        if(v != 0){
            continue;
        }
        res = fat_set(fs, c, fs->eoc);
        if(res < 0){
            return res;
        }
        if(zero){
            res = fat_zero_cluster(fs, c);
            if(res < 0){
                return res;
            }
        }
        if(prev){
            res = fat_set(fs, prev, c);
            if(res < 0){
                return res;
            }
        }
        if(fs->free_count != FAT_UNKNOWN && fs->free_count > 0){
            fs->free_count--;
        }
        fs->next_free    = c + 1;
        fs->fsinfo_dirty = true;
        *out = c;
        return 0;
    }
    return -ENOSPC;
}

static int fat_free_chain(struct fat_fs* fs, uint32_t cluster){
    for(uint32_t guard = 0; fat_cluster_valid(fs, cluster) && guard < fs->cluster_count; guard++){
        uint32_t next;
        int res = fat_get(fs, cluster, &next);
        if(res < 0){
            return res;
        }
        res = fat_set(fs, cluster, 0);
        if(res < 0){
            return res;
        }
        if(fs->free_count != FAT_UNKNOWN){
            fs->free_count++;
        }
        fs->fsinfo_dirty = true;
        if(fat_is_eoc(fs, next)){
            break;
        }
        cluster = next;
    }
    return 0;
}

// FAT32 keeps a free-cluster hint in the FSInfo sector. Linux
// trusts it for df, and fsck.fat checks it.
static void fat_flush_fsinfo(struct fat_fs* fs){
    if(!fs->fsinfo_dirty || fs->type != 32 || !fs->fsinfo_sector){
        return;
    }
    uint64_t base = (uint64_t)fs->fsinfo_sector * fs->bps;
    uint32_t lead = 0, sig = 0;
    if(fat_rw(fs, base, &lead, 4, false) < 0 || fat_rw(fs, base + 484, &sig, 4, false) < 0 ||
       lead != FAT_FSINFO_LEAD_SIG || sig != FAT_FSINFO_STRUCT_SIG){
        return;
    }
    uint32_t vals[2] = { fs->free_count, fs->next_free };
    fat_rw(fs, base + 488, vals, sizeof(vals), true);
    fs->fsinfo_dirty = false;
}

static uint32_t fat_item_cluster(struct fat_fs* fs, struct fat_directory_item* item){
    uint32_t c = item->low_16_bits_first_cluster;
    if(fs->type == 32){
        c |= (uint32_t)item->high_16_bits_first_cluster << 16;
    }
    return c;
}

static void fat_item_set_cluster(struct fat_directory_item* item, uint32_t cluster){
    item->low_16_bits_first_cluster  = (uint16_t)cluster;
    item->high_16_bits_first_cluster = (uint16_t)(cluster >> 16);
}

// A directory entry's cluster, as a directory id ("..": 0 = root).
static uint32_t fat_dir_id(struct fat_fs* fs, uint32_t cluster){
    return cluster == 0 ? fs->root_id : cluster;
}

// ---------------------------------------------------------------
// Directories
// ---------------------------------------------------------------

// Byte offset of entry `index`. Returns -ENOSPC past the end of
// the directory unless `extend` (then a zeroed cluster is added).
static int fat_dir_offset(struct fat_fs* fs, struct fat_dir_cursor* cur, uint32_t index,
                          bool extend, uint64_t* off){
    if(index >= FAT_MAX_DIR_ENTRIES){
        return -ENOSPC;
    }
    if(cur->first == 0){
        if(index >= fs->root_entries){
            return -ENOSPC;
        }
        *off = (uint64_t)fs->root_dir_sector * fs->bps + (uint64_t)index * 32;
        return 0;
    }
    uint32_t per_cluster = fs->cluster_bytes / 32;
    uint32_t want = index / per_cluster;
    if(cur->cluster == 0 || want < cur->cidx){
        cur->cidx    = 0;
        cur->cluster = cur->first;
    }
    while(cur->cidx < want){
        uint32_t next;
        int res = fat_get(fs, cur->cluster, &next);
        if(res < 0){
            return res;
        }
        if(fat_is_eoc(fs, next)){
            if(!extend){
                return -ENOSPC;
            }
            res = fat_alloc_cluster(fs, cur->cluster, true, &next);
            if(res < 0){
                return res;
            }
        } else if(!fat_cluster_valid(fs, next)){
            return -EIO;
        }
        cur->cluster = next;
        cur->cidx++;
    }
    *off = fat_cluster_offset(fs, cur->cluster) + (uint64_t)(index % per_cluster) * 32;
    return 0;
}

static int fat_dir_entry_rw(struct fat_fs* fs, uint32_t dir, uint32_t index,
                            struct fat_directory_item* item, bool write){
    struct fat_dir_cursor cur = { dir, 0, 0 };
    uint64_t off;
    int res = fat_dir_offset(fs, &cur, index, false, &off);
    if(res < 0){
        return res;
    }
    return fat_rw(fs, off, item, sizeof(*item), write);
}

static uint8_t fat_lfn_checksum(const uint8_t* name11){
    uint8_t sum = 0;
    for(int i = 0; i < 11; i++){
        sum = (uint8_t)(((sum & 1) ? 0x80 : 0) + (sum >> 1) + name11[i]);
    }
    return sum;
}

static int fat_ucs2_to_utf8(const uint16_t* in, char* out, int max){
    int o = 0;
    for(int i = 0; in[i] && in[i] != 0xFFFF; i++){
        uint16_t c = in[i];
        int need = c < 0x80 ? 1 : (c < 0x800 ? 2 : 3);
        if(o + need >= max){
            break;
        }
        if(need == 1){
            out[o++] = (char)c;
        } else if(need == 2){
            out[o++] = (char)(0xC0 | (c >> 6));
            out[o++] = (char)(0x80 | (c & 0x3F));
        } else {
            out[o++] = (char)(0xE0 | (c >> 12));
            out[o++] = (char)(0x80 | ((c >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (c & 0x3F));
        }
    }
    out[o] = 0;
    return o;
}

// Decode UTF-8 to UCS-2. Returns the character count or -1.
static int fat_utf8_to_ucs2(const char* in, uint16_t* out, int max){
    int n = 0;
    const uint8_t* p = (const uint8_t*)in;
    while(*p){
        uint32_t c;
        if(p[0] < 0x80){
            c = p[0];
            p += 1;
        } else if((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80){
            c = ((uint32_t)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
            p += 2;
        } else if((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80){
            c = ((uint32_t)(p[0] & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            p += 3;
        } else {
            return -1;   // invalid, or outside the BMP
        }
        if(n >= max){
            return -1;
        }
        out[n++] = (uint16_t)c;
    }
    return n;
}

static void fat_short_to_string(struct fat_directory_item* item, char* out){
    int o = 0;
    for(int i = 0; i < 8 && item->filename[i] != ' '; i++){
        char c = (char)item->filename[i];
        if(i == 0 && item->filename[0] == FAT_ENTRY_KANJI_E5){
            c = (char)0xE5;
        }
        out[o++] = (item->nt_case & FAT_NT_LOWER_BASE) ? tolower(c) : c;
    }
    if(item->ext[0] != ' '){
        out[o++] = '.';
        for(int i = 0; i < 3 && item->ext[i] != ' '; i++){
            char c = (char)item->ext[i];
            out[o++] = (item->nt_case & FAT_NT_LOWER_EXT) ? tolower(c) : c;
        }
    }
    out[o] = 0;
}

static void fat_scan_init(struct fat_scan* s, uint32_t dir){
    memset(s, 0, sizeof(*s));
    s->cur.first = dir;
}

// Next live entry (skipping deleted entries, volume labels and
// LFN fragments). Returns 1 with *out filled, 0 at the end of the
// directory, or a negative status.
static int fat_scan_next(struct fat_fs* fs, struct fat_scan* s, struct fat_entry_info* out){
    for(;;){
        uint64_t off;
        int res = fat_dir_offset(fs, &s->cur, s->index, false, &off);
        if(res == -ENOSPC){
            return 0;
        }
        if(res < 0){
            return res;
        }
        struct fat_directory_item e;
        res = fat_rw(fs, off, &e, sizeof(e), false);
        if(res < 0){
            return res;
        }
        uint32_t idx = s->index++;

        if(e.filename[0] == FAT_ENTRY_END){
            s->index--;          // stay at the end marker
            return 0;
        }
        if(e.filename[0] == FAT_ENTRY_DELETED){
            s->lfn_valid = false;
            continue;
        }
        if((e.attribute & FAT_ATTR_LFN_MASK) == FAT_ATTR_LFN){
            struct fat_lfn_entry* l = (struct fat_lfn_entry*)&e;
            int seq = l->seq & 0x1F;
            if(l->seq & FAT_LFN_LAST){
                if(seq < 1 || seq > FAT_LFN_MAX_ENTRIES){
                    s->lfn_valid = false;
                    continue;
                }
                s->lfn_valid = true;
                s->lfn_sum   = l->checksum;
                s->lfn_start = idx;
                memset(s->lfn, 0, sizeof(s->lfn));
            } else if(!s->lfn_valid || seq != s->lfn_next || l->checksum != s->lfn_sum){
                s->lfn_valid = false;
                continue;
            }
            uint16_t* dst = &s->lfn[(seq - 1) * FAT_LFN_CHARS];
            memcpy(dst,      l->name1, sizeof(l->name1));
            memcpy(dst + 5,  l->name2, sizeof(l->name2));
            memcpy(dst + 11, l->name3, sizeof(l->name3));
            s->lfn_next = seq - 1;
            continue;
        }

        bool have_lfn = s->lfn_valid && s->lfn_next == 0 &&
                        fat_lfn_checksum(e.filename) == s->lfn_sum;
        uint32_t first = have_lfn ? s->lfn_start : idx;
        s->lfn_valid = false;
        if(e.attribute & FAT_ATTR_VOLUME_ID){
            continue;
        }
        memcpy(&out->item, &e, sizeof(e));
        out->index       = idx;
        out->first_index = first;
        fat_short_to_string(&e, out->short_name);
        if(have_lfn){
            fat_ucs2_to_utf8(s->lfn, out->name, sizeof(out->name));
        } else {
            strcpy(out->name, out->short_name);
        }
        return 1;
    }
}

static bool fat_name_equal(const char* a, const char* b){
    while(*a && *b){
        if(tolower(*a) != tolower(*b)){
            return false;
        }
        a++;
        b++;
    }
    return *a == *b;
}

// Look `name` up in directory `dir` (long or 8.3 name, ASCII
// case-insensitive).
static int fat_dir_find(struct fat_fs* fs, uint32_t dir, const char* name,
                        struct fat_entry_info* out){
    struct fat_scan* s = kzalloc(sizeof(*s));
    if(!s){
        return -ENOMEM;
    }
    fat_scan_init(s, dir);
    int res;
    bool found = false;
    while((res = fat_scan_next(fs, s, out)) == 1){
        if(fat_name_equal(out->name, name) || fat_name_equal(out->short_name, name)){
            found = true;
            break;
        }
    }
    kfree(s);
    if(res < 0){
        return res;
    }
    return found ? 0 : -ENOENT;
}

// Walk a path. On success *info describes the last component
// and *parent_out is the directory holding it. When only the
// last component is missing, returns -ENOENT with *parent_out
// and *last_out still valid (used by create and mkdir).
static int fat_walk(struct fat_fs* fs, struct path_part* path, uint32_t* parent_out,
                    struct fat_entry_info* info, const char** last_out){
    uint32_t dir = fs->root_id;
    for(struct path_part* part = path; part; part = part->next){
        *parent_out = dir;
        *last_out   = part->part;
        int res = fat_dir_find(fs, dir, part->part, info);
        if(res < 0){
            if(res == -ENOENT && part->next){
                return -EBADPATH;
            }
            return res;
        }
        if(part->next){
            if(!(info->item.attribute & FAT_ATTR_DIRECTORY)){
                return -ENOTDIR;
            }
            dir = fat_dir_id(fs, fat_item_cluster(fs, &info->item));
        }
    }
    return 0;
}

// ---------------------------------------------------------------
// Names
// ---------------------------------------------------------------

static bool fat_long_char_ok(char c){
    const char* bad = "\"*/:<>?\\|";
    if((uint8_t)c < 0x20 || c == 0x7F){
        return false;
    }
    for(const char* b = bad; *b; b++){
        if(*b == c){
            return false;
        }
    }
    return true;
}

static bool fat_short_char_ok(char c){
    const char* ok = "!#$%&'()-@^_`{}~";
    if((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')){
        return true;
    }
    for(const char* o = ok; *o; o++){
        if(*o == c){
            return true;
        }
    }
    return false;
}

static char fat_toupper(char c){
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static int fat_validate_name(const char* name){
    int len = strnlen(name, FAT_NAME_BUF);
    if(len == 0 || len >= FAT_NAME_BUF){
        return -EBADPATH;
    }
    if(strncmp(name, ".", 2) == 0 || strncmp(name, "..", 3) == 0){
        return -EBADPATH;
    }
    if(name[len - 1] == ' ' || name[len - 1] == '.'){
        return -EBADPATH;
    }
    for(int i = 0; i < len; i++){
        if(!fat_long_char_ok(name[i])){
            return -EBADPATH;
        }
    }
    return 0;
}

// Can `name` be stored as a bare 8.3 entry? Each part must be
// all upper or all lower case (lower case uses the NT flags, as
// Windows and Linux do).
static bool fat_name_to_short(const char* name, uint8_t out[11], uint8_t* nt_case){
    int len = strlen(name);
    int dot = -1;
    for(int i = 0; i < len; i++){
        if(name[i] == '.'){
            if(dot >= 0){
                return false;
            }
            dot = i;
        }
    }
    int base_len = dot < 0 ? len : dot;
    int ext_len  = dot < 0 ? 0 : len - dot - 1;
    if(base_len < 1 || base_len > 8 || ext_len > 3 || (dot >= 0 && ext_len == 0)){
        return false;
    }
    memset(out, ' ', 11);
    *nt_case = 0;
    for(int part = 0; part < 2; part++){
        const char* src = part == 0 ? name : name + dot + 1;
        int n = part == 0 ? base_len : ext_len;
        bool lower = false, upper = false;
        for(int i = 0; i < n; i++){
            char c = src[i];
            if(c >= 'a' && c <= 'z'){
                lower = true;
            } else if(c >= 'A' && c <= 'Z'){
                upper = true;
            }
            c = fat_toupper(c);
            if(!fat_short_char_ok(c)){
                return false;
            }
            out[(part == 0 ? 0 : 8) + i] = (uint8_t)c;
        }
        if(lower && upper){
            return false;
        }
        if(lower){
            *nt_case |= part == 0 ? FAT_NT_LOWER_BASE : FAT_NT_LOWER_EXT;
        }
    }
    return true;
}

static bool fat_short_exists(struct fat_fs* fs, uint32_t dir, const uint8_t name11[11]){
    struct fat_scan* s = kzalloc(sizeof(*s));
    struct fat_entry_info* info = kzalloc(sizeof(*info));
    bool found = false;
    if(!s || !info){
        found = true;     // be safe: treat as taken
        goto out;
    }
    fat_scan_init(s, dir);
    while(fat_scan_next(fs, s, info) == 1){
        if(memcmp(info->item.filename, (void*)name11, 11) == 0){
            found = true;
            break;
        }
    }
out:
    kfree(s);
    kfree(info);
    return found;
}

// Make a unique "BASIS~N.EXT" alias for a long name.
static int fat_make_short_alias(struct fat_fs* fs, uint32_t dir, const char* name, uint8_t out[11]){
    char base[9] = {0};
    char ext[4]  = {0};
    int len = strlen(name);
    int last_dot = -1;
    for(int i = 0; i < len; i++){
        if(name[i] == '.'){
            last_dot = i;
        }
    }
    int b = 0;
    for(int i = 0; i < (last_dot > 0 ? last_dot : len) && b < 8; i++){
        char c = fat_toupper(name[i]);
        if(c == ' ' || c == '.'){
            continue;
        }
        base[b++] = fat_short_char_ok(c) ? c : '_';
    }
    if(b == 0){
        base[b++] = '_';
    }
    if(last_dot > 0){
        int e = 0;
        for(int i = last_dot + 1; i < len && e < 3; i++){
            char c = fat_toupper(name[i]);
            if(c == ' '){
                continue;
            }
            ext[e++] = fat_short_char_ok(c) ? c : '_';
        }
    }

    for(uint32_t n = 1; n < 1000000; n++){
        char tail[8];
        int t = 0;
        char digits[8];
        int d = 0;
        for(uint32_t v = n; v; v /= 10){
            digits[d++] = (char)('0' + v % 10);
        }
        tail[t++] = '~';
        while(d){
            tail[t++] = digits[--d];
        }
        int keep = b < 8 - t ? b : 8 - t;
        memset(out, ' ', 11);
        memcpy(out, base, keep);
        memcpy(out + keep, tail, t);
        memcpy(out + 8, ext, strlen(ext));
        if(!fat_short_exists(fs, dir, out)){
            return 0;
        }
    }
    return -EEXIST;
}

// Create a directory entry (plus LFN entries when needed) for
// `name` in `dir`. *index_out is the 8.3 entry's index.
static int fat_add_entry(struct fat_fs* fs, uint32_t dir, const char* name, uint8_t attr,
                         uint32_t cluster, uint32_t* index_out){
    int res = fat_validate_name(name);
    if(res < 0){
        return res;
    }
    uint8_t short11[11];
    uint8_t nt_case = 0;
    uint16_t* ucs = kzalloc(sizeof(uint16_t) * (FAT_LFN_MAX_CHARS + 1));
    if(!ucs){
        return -ENOMEM;
    }
    int ucs_len = 0;
    int lfn_entries = 0;
    if(!fat_name_to_short(name, short11, &nt_case)){
        ucs_len = fat_utf8_to_ucs2(name, ucs, FAT_LFN_MAX_CHARS);
        if(ucs_len <= 0){
            res = -EBADPATH;
            goto out;
        }
        lfn_entries = (ucs_len + FAT_LFN_CHARS - 1) / FAT_LFN_CHARS;
        res = fat_make_short_alias(fs, dir, name, short11);
        if(res < 0){
            goto out;
        }
    }
    if(short11[0] == FAT_ENTRY_DELETED){
        short11[0] = FAT_ENTRY_KANJI_E5;
    }

    // Find `needed` consecutive free entries; grow the directory
    // when there is no room (not possible for the fixed root).
    int needed = lfn_entries + 1;
    struct fat_dir_cursor cur = { dir, 0, 0 };
    uint32_t run_start = 0;
    int run = 0;
    for(uint32_t idx = 0; run < needed; idx++){
        uint64_t off;
        res = fat_dir_offset(fs, &cur, idx, true, &off);
        if(res < 0){
            goto out;
        }
        uint8_t first = 0;
        res = fat_rw(fs, off, &first, 1, false);
        if(res < 0){
            goto out;
        }
        if(first == FAT_ENTRY_END || first == FAT_ENTRY_DELETED){
            if(run == 0){
                run_start = idx;
            }
            run++;
        } else {
            run = 0;
        }
    }

    uint8_t sum = fat_lfn_checksum(short11);
    for(int k = 0; k < lfn_entries; k++){
        int seq = lfn_entries - k;
        struct fat_lfn_entry l;
        memset(&l, 0, sizeof(l));
        l.seq       = (uint8_t)(seq | (k == 0 ? FAT_LFN_LAST : 0));
        l.attribute = FAT_ATTR_LFN;
        l.checksum  = sum;
        uint16_t chars[FAT_LFN_CHARS];
        for(int j = 0; j < FAT_LFN_CHARS; j++){
            int pos = (seq - 1) * FAT_LFN_CHARS + j;
            chars[j] = pos < ucs_len ? ucs[pos] : (pos == ucs_len ? 0x0000 : 0xFFFF);
        }
        memcpy(l.name1, chars,      sizeof(l.name1));
        memcpy(l.name2, chars + 5,  sizeof(l.name2));
        memcpy(l.name3, chars + 11, sizeof(l.name3));
        res = fat_dir_entry_rw(fs, dir, run_start + k, (struct fat_directory_item*)&l, true);
        if(res < 0){
            goto out;
        }
    }

    struct fat_directory_item item;
    memset(&item, 0, sizeof(item));
    memcpy(item.filename, short11, 8);
    memcpy(item.ext, short11 + 8, 3);
    item.attribute     = attr;
    item.nt_case       = nt_case;
    uint16_t now_date, now_time;
    fat_now(&now_date, &now_time);
    item.creation_time = now_time;
    item.creation_date = now_date;
    item.last_access   = now_date;
    item.last_mod_time = now_time;
    item.last_mod_date = now_date;
    fat_item_set_cluster(&item, cluster);
    res = fat_dir_entry_rw(fs, dir, run_start + lfn_entries, &item, true);
    if(res == 0){
        *index_out = run_start + lfn_entries;
    }
out:
    kfree(ucs);
    return res;
}

static int fat_remove_entry(struct fat_fs* fs, uint32_t dir, uint32_t first, uint32_t last){
    struct fat_dir_cursor cur = { dir, 0, 0 };
    for(uint32_t idx = first; idx <= last; idx++){
        uint64_t off;
        int res = fat_dir_offset(fs, &cur, idx, false, &off);
        if(res < 0){
            return res;
        }
        uint8_t mark = FAT_ENTRY_DELETED;
        res = fat_rw(fs, off, &mark, 1, true);
        if(res < 0){
            return res;
        }
    }
    return 0;
}

// ---------------------------------------------------------------
// Mount
// ---------------------------------------------------------------

static void fat_read_root_label(struct fat_fs* fs){
    struct fat_dir_cursor cur = { fs->root_id, 0, 0 };
    for(uint32_t idx = 0; idx < 512; idx++){
        uint64_t off;
        struct fat_directory_item e;
        if(fat_dir_offset(fs, &cur, idx, false, &off) < 0 ||
           fat_rw(fs, off, &e, sizeof(e), false) < 0 ||
           e.filename[0] == FAT_ENTRY_END){
            return;
        }
        if(e.filename[0] != FAT_ENTRY_DELETED &&
           (e.attribute & FAT_ATTR_LFN_MASK) != FAT_ATTR_LFN &&
           (e.attribute & FAT_ATTR_VOLUME_ID)){
            memcpy(fs->label, e.filename, 11);
            fs->label[11] = 0;
            return;
        }
    }
}

static int fat_resolve(struct disk* disk){
    int res = 0;
    struct fat_fs* fs = NULL;
    int ss = disk->sector_size < 512 ? 512 : disk->sector_size;
    uint8_t* sector = kzalloc(ss);
    if(!sector){
        return -ENOMEM;
    }
    if(disk_read_block(disk, 0, 1, sector) < 0){
        res = -EIO;
        goto out;
    }

    struct fat_bpb* bpb = (struct fat_bpb*)sector;
    struct fat_ebpb16* e16 = (struct fat_ebpb16*)(sector + sizeof(struct fat_bpb));
    struct fat_ebpb32* e32 = (struct fat_ebpb32*)(sector + sizeof(struct fat_bpb));
    uint32_t bps = bpb->bytes_per_sector;
    uint32_t spc = bpb->sectors_per_cluster;
    if((sector[0] != 0xEB && sector[0] != 0xE9) ||
       (bps != 512 && bps != 1024 && bps != 2048 && bps != 4096) ||
       spc == 0 || (spc & (spc - 1)) != 0 || bps * spc > 64 * 1024 ||
       bpb->reserved_sectors == 0 || bpb->fat_count == 0){
        res = -EFSNOTUS;
        goto out;
    }
    uint32_t fat_sectors   = bpb->fat_size16 ? bpb->fat_size16 : e32->fat_size32;
    uint32_t total_sectors = bpb->total_sectors16 ? bpb->total_sectors16 : bpb->total_sectors32;
    uint32_t root_sectors  = ((uint32_t)bpb->root_entries * 32 + bps - 1) / bps;
    uint64_t meta = (uint64_t)bpb->reserved_sectors + (uint64_t)bpb->fat_count * fat_sectors + root_sectors;
    if(fat_sectors == 0 || total_sectors == 0 || meta >= total_sectors){
        res = -EFSNOTUS;
        goto out;
    }
    if(disk->total_sectors &&
       (uint64_t)total_sectors * bps > disk->total_sectors * (uint64_t)disk->sector_size){
        res = -EFSNOTUS;      // volume claims to be bigger than the disk
        goto out;
    }

    fs = kzalloc(sizeof(struct fat_fs));
    if(!fs){
        res = -ENOMEM;
        goto out;
    }
    fs->disk              = disk;
    fs->bps               = bps;
    fs->spc               = spc;
    fs->cluster_bytes     = bps * spc;
    fs->reserved          = bpb->reserved_sectors;
    fs->fat_count         = bpb->fat_count;
    fs->fat_sectors       = fat_sectors;
    fs->active_fat        = -1;
    fs->root_entries      = bpb->root_entries;
    fs->root_dir_sector   = bpb->reserved_sectors + bpb->fat_count * fat_sectors;
    fs->root_dir_sectors  = root_sectors;
    fs->first_data_sector = (uint32_t)meta;
    fs->cluster_count     = (total_sectors - (uint32_t)meta) / spc;
    fs->free_count        = FAT_UNKNOWN;
    fs->next_free         = FAT_UNKNOWN;

    // FAT type by cluster count, per the Microsoft spec.
    const uint8_t* label = NULL;
    if(fs->cluster_count < 4085){
        fs->type    = 12;
        fs->eoc_min = 0xFF8;
        fs->eoc     = 0xFFF;
    } else if(fs->cluster_count < 65525){
        fs->type    = 16;
        fs->eoc_min = 0xFFF8;
        fs->eoc     = 0xFFFF;
    } else {
        fs->type    = 32;
        fs->eoc_min = 0x0FFFFFF8;
        fs->eoc     = 0x0FFFFFFF;
    }
    if(fs->type == 32){
        if(bpb->root_entries != 0 || bpb->fat_size16 != 0 ||
           e32->root_cluster < 2 || e32->root_cluster >= fs->cluster_count + 2){
            res = -EFSNOTUS;
            goto out;
        }
        fs->root_id = e32->root_cluster;
        if(e32->ext_flags & 0x80){
            fs->active_fat = e32->ext_flags & 0x0F;
            if((uint32_t)fs->active_fat >= fs->fat_count){
                res = -EFSNOTUS;
                goto out;
            }
        }
        if(e32->fsinfo_sector != 0 && e32->fsinfo_sector != 0xFFFF &&
           e32->fsinfo_sector < fs->reserved){
            fs->fsinfo_sector = e32->fsinfo_sector;
        }
        if(e32->boot_signature == 0x29){
            label = e32->label;
        }
    } else {
        if(bpb->root_entries == 0){
            res = -EFSNOTUS;
            goto out;
        }
        fs->root_id = 0;
        if(e16->boot_signature == 0x29){
            label = e16->label;
        }
    }

    fs->stream = diskstreamer_new_from_disk(disk);
    if(!fs->stream){
        res = -ENOMEM;
        goto out;
    }

    memcpy(fs->label, "NO NAME    ", 11);
    if(label){
        memcpy(fs->label, (void*)label, 11);
    }
    fs->label[11] = 0;
    // The root-directory volume entry is the authoritative label.
    fat_read_root_label(fs);

    if(fs->fsinfo_sector){
        uint64_t base = (uint64_t)fs->fsinfo_sector * bps;
        uint32_t lead = 0, sig = 0, vals[2] = {0, 0};
        if(fat_rw(fs, base, &lead, 4, false) == 0 && fat_rw(fs, base + 484, &sig, 4, false) == 0 &&
           lead == FAT_FSINFO_LEAD_SIG && sig == FAT_FSINFO_STRUCT_SIG &&
           fat_rw(fs, base + 488, vals, sizeof(vals), false) == 0){
            if(vals[0] <= fs->cluster_count){
                fs->free_count = vals[0];
            }
            fs->next_free = vals[1];
        } else {
            fs->fsinfo_sector = 0;
        }
    }

    disk->fs_private = fs;

out:
    kfree(sector);
    if(res < 0 && fs){
        if(fs->stream){
            diskstreamer_close(fs->stream);
        }
        kfree(fs);
    }
    return res;
}

static int fat_volume_name(void* private, char* name_out, size_t max){
    struct fat_fs* fs = private;
    if(!fs || max == 0){
        return -EINVARG;
    }
    size_t n = max - 1 < 11 ? max - 1 : 11;
    memcpy(name_out, fs->label, n);
    name_out[n] = 0;
    return 0;
}

// ---------------------------------------------------------------
// Files
// ---------------------------------------------------------------

static struct fat_file* fat_file_from_info(struct fat_fs* fs, uint32_t parent,
                                           struct fat_entry_info* info, FILE_MODE mode){
    struct fat_file* f = kzalloc(sizeof(struct fat_file));
    if(!f){
        return NULL;
    }
    f->fs            = fs;
    f->parent        = parent;
    f->index         = info->index;
    f->attr          = info->item.attribute;
    f->is_dir        = (info->item.attribute & FAT_ATTR_DIRECTORY) != 0;
    f->first_cluster = fat_item_cluster(fs, &info->item);
    f->size          = f->is_dir ? 0 : info->item.filesize;
    f->mode          = mode;
    return f;
}

// Write the file's first cluster, size and mtime back into its
// directory entry.
static int fat_file_sync_entry(struct fat_file* f){
    struct fat_directory_item item;
    int res = fat_dir_entry_rw(f->fs, f->parent, f->index, &item, false);
    if(res < 0){
        return res;
    }
    fat_item_set_cluster(&item, f->first_cluster);
    item.filesize      = f->size;
    uint16_t now_date, now_time;
    fat_now(&now_date, &now_time);
    item.last_mod_date = now_date;
    item.last_mod_time = now_time;
    item.last_access   = now_date;
    item.attribute    |= FAT_ATTR_ARCHIVE;
    return fat_dir_entry_rw(f->fs, f->parent, f->index, &item, true);
}

// Cluster number of the file's cluster `cidx` (0-based). With
// `extend`, missing clusters are allocated.
static int fat_file_cluster(struct fat_file* f, uint32_t cidx, bool extend, uint32_t* out){
    struct fat_fs* fs = f->fs;
    if(f->first_cluster == 0){
        if(!extend){
            return -EIO;
        }
        int res = fat_alloc_cluster(fs, 0, false, &f->first_cluster);
        if(res < 0){
            return res;
        }
        f->cache_cidx    = 0;
        f->cache_cluster = f->first_cluster;
    }
    uint32_t cur = f->first_cluster;
    uint32_t i = 0;
    if(f->cache_cluster && f->cache_cidx <= cidx){
        cur = f->cache_cluster;
        i   = f->cache_cidx;
    }
    while(i < cidx){
        uint32_t next;
        int res = fat_get(fs, cur, &next);
        if(res < 0){
            return res;
        }
        if(fat_is_eoc(fs, next)){
            if(!extend){
                return -EIO;
            }
            res = fat_alloc_cluster(fs, cur, false, &next);
            if(res < 0){
                return res;
            }
        } else if(!fat_cluster_valid(fs, next)){
            return -EIO;
        }
        cur = next;
        i++;
    }
    f->cache_cidx    = i;
    f->cache_cluster = cur;
    *out = cur;
    return 0;
}

static int fat_file_read_bytes(struct fat_file* f, char* out, uint32_t len){
    struct fat_fs* fs = f->fs;
    uint32_t done = 0;
    while(done < len){
        uint32_t cidx = f->pos / fs->cluster_bytes;
        uint32_t off  = f->pos % fs->cluster_bytes;
        uint32_t cluster;
        int res = fat_file_cluster(f, cidx, false, &cluster);
        if(res < 0){
            return res;
        }
        // Merge physically contiguous clusters into one read.
        uint32_t run = 1;
        uint32_t last = cluster;
        while((uint64_t)run * fs->cluster_bytes - off < len - done && run < 64){
            uint32_t next;
            if(fat_get(fs, last, &next) < 0 || next != last + 1 || !fat_cluster_valid(fs, next)){
                break;
            }
            last = next;
            run++;
        }
        uint64_t chunk = (uint64_t)run * fs->cluster_bytes - off;
        if(chunk > len - done){
            chunk = len - done;
        }
        res = fat_rw(fs, fat_cluster_offset(fs, cluster) + off, out + done, (int)chunk, false);
        if(res < 0){
            return res;
        }
        f->cache_cidx    = cidx + run - 1;
        f->cache_cluster = last;
        f->pos += chunk;
        done   += chunk;
    }
    return 0;
}

// Write at f->pos, allocating clusters as needed. Updates pos and
// size but not the directory entry.
static int fat_file_write_bytes(struct fat_file* f, const char* in, uint32_t len){
    struct fat_fs* fs = f->fs;
    uint32_t done = 0;
    while(done < len){
        uint32_t cidx = f->pos / fs->cluster_bytes;
        uint32_t off  = f->pos % fs->cluster_bytes;
        uint32_t cluster;
        int res = fat_file_cluster(f, cidx, true, &cluster);
        if(res < 0){
            return res;
        }
        uint32_t chunk = fs->cluster_bytes - off;
        if(chunk > len - done){
            chunk = len - done;
        }
        res = fat_rw(fs, fat_cluster_offset(fs, cluster) + off, (void*)(in + done), chunk, true);
        if(res < 0){
            return res;
        }
        f->pos += chunk;
        done   += chunk;
        if(f->pos > f->size){
            f->size = f->pos;
        }
    }
    return 0;
}

static int fat_file_zero_fill(struct fat_file* f, uint32_t until){
    static const char zeros[512] = {0};
    while(f->pos < until){
        uint32_t chunk = until - f->pos > sizeof(zeros) ? sizeof(zeros) : until - f->pos;
        int res = fat_file_write_bytes(f, zeros, chunk);
        if(res < 0){
            return res;
        }
    }
    return 0;
}

static int fat_file_shrink(struct fat_file* f, uint32_t new_size){
    struct fat_fs* fs = f->fs;
    uint32_t keep = (new_size + fs->cluster_bytes - 1) / fs->cluster_bytes;
    int res = 0;
    if(f->first_cluster == 0){
        f->size = new_size;
        return 0;
    }
    if(keep == 0){
        res = fat_free_chain(fs, f->first_cluster);
        f->first_cluster = 0;
    } else {
        uint32_t last;
        res = fat_file_cluster(f, keep - 1, false, &last);
        if(res < 0){
            return res;
        }
        uint32_t next;
        res = fat_get(fs, last, &next);
        if(res < 0){
            return res;
        }
        res = fat_set(fs, last, fs->eoc);
        if(res == 0 && !fat_is_eoc(fs, next) && fat_cluster_valid(fs, next)){
            res = fat_free_chain(fs, next);
        }
    }
    f->cache_cluster = 0;
    f->size = new_size;
    return res;
}

static bool fat_file_writable(struct fat_file* f){
    return !f->fs->disk->read_only && !f->is_dir && f->mode != FILE_MODE_READ &&
           !(f->attr & FAT_ATTR_READ_ONLY);
}

static void* fat_open(struct disk* disk, struct path_part* path, FILE_MODE mode){
    struct fat_fs* fs = disk->fs_private;
    struct fat_entry_info* info = kzalloc(sizeof(*info));
    struct fat_file* f = NULL;
    uint32_t parent = 0;
    const char* last = NULL;
    int res;
    if(!info){
        return ERROR(-ENOMEM);
    }

    res = fat_walk(fs, path, &parent, info, &last);
    if(res == 0){
        bool is_dir = (info->item.attribute & FAT_ATTR_DIRECTORY) != 0;
        if(mode != FILE_MODE_READ){
            if(is_dir){
                res = -EISDIR;
                goto out;
            }
            if(disk->read_only || (info->item.attribute & FAT_ATTR_READ_ONLY)){
                res = -ERDONLY;
                goto out;
            }
        }
        f = fat_file_from_info(fs, parent, info, mode);
        if(!f){
            res = -ENOMEM;
            goto out;
        }
        if(mode == FILE_MODE_WRITE && (f->size || f->first_cluster)){
            res = fat_file_shrink(f, 0);
            if(res == 0){
                res = fat_file_sync_entry(f);
            }
        } else if(mode == FILE_MODE_APPEND){
            f->pos = f->size;
        }
    } else if(res == -ENOENT && mode != FILE_MODE_READ){
        if(disk->read_only){
            res = -ERDONLY;
            goto out;
        }
        uint32_t index;
        res = fat_add_entry(fs, parent, last, FAT_ATTR_ARCHIVE, 0, &index);
        if(res < 0){
            goto out;
        }
        f = kzalloc(sizeof(struct fat_file));
        if(!f){
            res = -ENOMEM;
            goto out;
        }
        f->fs     = fs;
        f->parent = parent;
        f->index  = index;
        f->attr   = FAT_ATTR_ARCHIVE;
        f->mode   = mode;
    }
out:
    fat_flush_fsinfo(fs);
    kfree(info);
    if(res < 0){
        kfree(f);
        return ERROR(res);
    }
    return f;
}

static int fat_read(struct disk* disk, void* private, uint32_t size, uint32_t nmemb, char* out){
    struct fat_file* f = private;
    if(f->is_dir){
        return -EISDIR;
    }
    uint64_t want = (uint64_t)size * nmemb;
    if(f->pos >= f->size){
        return 0;
    }
    if(want > f->size - f->pos){
        want = f->size - f->pos;
    }
    uint32_t start = f->pos;
    int res = fat_file_read_bytes(f, out, (uint32_t)want);
    if(res < 0){
        return res;
    }
    return (int)((f->pos - start) / size);
}

static int fat_write(struct disk* disk, void* private, uint32_t size, uint32_t nmemb, const char* in){
    struct fat_file* f = private;
    if(f->is_dir){
        return -EISDIR;
    }
    if(!fat_file_writable(f)){
        return -ERDONLY;
    }
    uint64_t total = (uint64_t)size * nmemb;
    if(f->mode == FILE_MODE_APPEND){
        f->pos = f->size;
    }
    if((uint64_t)f->pos + total > FAT_MAX_FILE_SIZE || total > 0x7FFFFFFF){
        return -ENOSPC;
    }
    int res = 0;
    if(f->pos > f->size){
        // A seek past the end leaves a gap that reads as zeros.
        uint32_t target = f->pos;
        f->pos = f->size;
        res = fat_file_zero_fill(f, target);
    }
    uint32_t start = f->pos;
    if(res == 0){
        res = fat_file_write_bytes(f, in, (uint32_t)total);
    }
    int sync = fat_file_sync_entry(f);
    fat_flush_fsinfo(f->fs);
    if(res < 0){
        return res;
    }
    if(sync < 0){
        return sync;
    }
    return (int)((f->pos - start) / size);
}

static int fat_truncate(void* private, uint32_t size){
    struct fat_file* f = private;
    if(!fat_file_writable(f)){
        return f->is_dir ? -EISDIR : -ERDONLY;
    }
    int res;
    if(size > f->size){
        uint32_t saved = f->pos;
        f->pos = f->size;
        res = fat_file_zero_fill(f, size);
        f->pos = saved;
    } else {
        res = fat_file_shrink(f, size);
    }
    int sync = fat_file_sync_entry(f);
    fat_flush_fsinfo(f->fs);
    return res < 0 ? res : sync;
}

static int fat_seek(void* private, int64_t offset, FILE_SEEK_MODE seek_mode){
    struct fat_file* f = private;
    int64_t base;
    switch(seek_mode){
        case SEEK_SET: base = 0;       break;
        case SEEK_CUR: base = f->pos;  break;
        case SEEK_END: base = f->size; break;
        default:       return -EINVARG;
    }
    int64_t target = base + offset;
    // Seeking to or past the end is allowed: reads there return
    // 0 items and a write fills the gap with zeros.
    if(target < 0 || target > (int64_t)FAT_MAX_FILE_SIZE){
        return -EINVARG;
    }
    f->pos = (uint32_t)target;
    return 0;
}

static int fat_stat(struct disk* disk, void* private, struct file_stat* stat){
    struct fat_file* f = private;
    stat->filesize = f->size;
    stat->flags    = 0;
    if(f->attr & FAT_ATTR_READ_ONLY){
        stat->flags |= FILE_STAT_READ_ONLY;
    }
    if(f->is_dir){
        stat->flags |= FILE_STAT_DIRECTORY;
    }
    return 0;
}

static int fat_close(void* private){
    struct fat_file* f = private;
    kfree(f->scan);
    kfree(f);
    return 0;
}

// ---------------------------------------------------------------
// Directories: opendir/readdir, mkdir, unlink
// ---------------------------------------------------------------

static void* fat_opendir(struct disk* disk, struct path_part* path){
    struct fat_fs* fs = disk->fs_private;
    struct fat_file* f = NULL;
    uint32_t dir = fs->root_id;
    int res = 0;

    if(path){
        struct fat_entry_info* info = kzalloc(sizeof(*info));
        uint32_t parent;
        const char* last;
        if(!info){
            return ERROR(-ENOMEM);
        }
        res = fat_walk(fs, path, &parent, info, &last);
        if(res == 0 && !(info->item.attribute & FAT_ATTR_DIRECTORY)){
            res = -ENOTDIR;
        }
        if(res == 0){
            f = fat_file_from_info(fs, parent, info, FILE_MODE_READ);
            dir = fat_dir_id(fs, fat_item_cluster(fs, &info->item));
        }
        kfree(info);
    } else {
        f = kzalloc(sizeof(struct fat_file));
        if(f){
            f->fs      = fs;
            f->is_dir  = true;
            f->is_root = true;
            f->attr    = FAT_ATTR_DIRECTORY;
        }
    }
    if(res == 0 && !f){
        res = -ENOMEM;
    }
    if(res == 0){
        f->scan = kzalloc(sizeof(struct fat_scan));
        if(!f->scan){
            res = -ENOMEM;
        } else {
            fat_scan_init(f->scan, dir);
        }
    }
    if(res < 0){
        if(f){
            fat_close(f);
        }
        return ERROR(res);
    }
    return f;
}

static int fat_readdir(void* private, struct file_dirent* entry){
    struct fat_file* f = private;
    if(!f->scan){
        return -EINVARG;
    }
    struct fat_entry_info* info = kzalloc(sizeof(*info));
    if(!info){
        return -ENOMEM;
    }
    int res;
    while((res = fat_scan_next(f->fs, f->scan, info)) == 1){
        if(strncmp(info->name, ".", 2) == 0 || strncmp(info->name, "..", 3) == 0){
            continue;
        }
        memset(entry, 0, sizeof(*entry));
        strncpy(entry->name, info->name, FILE_DIRENT_NAME_MAX - 1);
        uint8_t a = info->item.attribute;
        entry->size  = (a & FAT_ATTR_DIRECTORY) ? 0 : info->item.filesize;
        entry->flags = ((a & FAT_ATTR_DIRECTORY) ? FILE_DIRENT_DIRECTORY : 0) |
                       ((a & FAT_ATTR_READ_ONLY) ? FILE_DIRENT_READ_ONLY : 0) |
                       ((a & FAT_ATTR_HIDDEN)    ? FILE_DIRENT_HIDDEN    : 0) |
                       ((a & FAT_ATTR_SYSTEM)    ? FILE_DIRENT_SYSTEM    : 0);
        break;
    }
    kfree(info);
    return res;
}

static int fat_dir_is_empty(struct fat_fs* fs, uint32_t dir){
    struct fat_scan* s = kzalloc(sizeof(*s));
    struct fat_entry_info* info = kzalloc(sizeof(*info));
    int res = 1;
    if(!s || !info){
        res = -ENOMEM;
        goto out;
    }
    fat_scan_init(s, dir);
    int r;
    while((r = fat_scan_next(fs, s, info)) == 1){
        if(strncmp(info->name, ".", 2) != 0 && strncmp(info->name, "..", 3) != 0){
            res = 0;
            break;
        }
    }
    if(r < 0){
        res = r;
    }
out:
    kfree(s);
    kfree(info);
    return res;
}

static int fat_mkdir(struct disk* disk, struct path_part* path){
    struct fat_fs* fs = disk->fs_private;
    struct fat_entry_info* info = kzalloc(sizeof(*info));
    uint32_t parent = 0;
    const char* last = NULL;
    uint32_t cluster = 0;
    int res;
    if(!info){
        return -ENOMEM;
    }
    if(disk->read_only){
        res = -ERDONLY;
        goto out;
    }
    res = fat_walk(fs, path, &parent, info, &last);
    if(res == 0){
        res = -EEXIST;
        goto out;
    }
    if(res != -ENOENT){
        goto out;
    }
    res = fat_validate_name(last);
    if(res < 0){
        goto out;
    }
    res = fat_alloc_cluster(fs, 0, true, &cluster);
    if(res < 0){
        goto out;
    }

    // "." and ".." first, so the directory is valid before it is
    // linked into its parent.
    struct fat_directory_item dots[2];
    memset(dots, 0, sizeof(dots));
    memset(dots[0].filename, ' ', 11);
    memset(dots[1].filename, ' ', 11);
    dots[0].filename[0] = '.';
    dots[1].filename[0] = '.';
    dots[1].filename[1] = '.';
    uint16_t now_date, now_time;
    fat_now(&now_date, &now_time);
    for(int i = 0; i < 2; i++){
        dots[i].attribute     = FAT_ATTR_DIRECTORY;
        dots[i].creation_time = now_time;
        dots[i].creation_date = now_date;
        dots[i].last_access   = now_date;
        dots[i].last_mod_time = now_time;
        dots[i].last_mod_date = now_date;
    }
    fat_item_set_cluster(&dots[0], cluster);
    fat_item_set_cluster(&dots[1], parent == fs->root_id ? 0 : parent);
    res = fat_rw(fs, fat_cluster_offset(fs, cluster), dots, sizeof(dots), true);
    if(res < 0){
        fat_free_chain(fs, cluster);
        goto out;
    }

    uint32_t index;
    res = fat_add_entry(fs, parent, last, FAT_ATTR_DIRECTORY, cluster, &index);
    if(res < 0){
        fat_free_chain(fs, cluster);
    }
out:
    fat_flush_fsinfo(fs);
    kfree(info);
    return res;
}

static int fat_unlink(struct disk* disk, struct path_part* path){
    struct fat_fs* fs = disk->fs_private;
    struct fat_entry_info* info = kzalloc(sizeof(*info));
    uint32_t parent = 0;
    const char* last = NULL;
    int res;
    if(!info){
        return -ENOMEM;
    }
    if(disk->read_only){
        res = -ERDONLY;
        goto out;
    }
    res = fat_walk(fs, path, &parent, info, &last);
    if(res < 0){
        goto out;
    }
    if(info->item.attribute & FAT_ATTR_READ_ONLY){
        res = -ERDONLY;
        goto out;
    }
    uint32_t cluster = fat_item_cluster(fs, &info->item);
    if(info->item.attribute & FAT_ATTR_DIRECTORY){
        if(strncmp(info->name, ".", 2) == 0 || strncmp(info->name, "..", 3) == 0){
            res = -EINVARG;
            goto out;
        }
        res = fat_dir_is_empty(fs, fat_dir_id(fs, cluster));
        if(res <= 0){
            res = res == 0 ? -ENOTEMPTY : res;
            goto out;
        }
    }
    // Remove the name first: a crash in between leaks clusters
    // (fsck finds them) instead of leaving a name that points at
    // freed clusters.
    res = fat_remove_entry(fs, parent, info->first_index, info->index);
    if(res == 0 && cluster){
        res = fat_free_chain(fs, cluster);
    }
out:
    fat_flush_fsinfo(fs);
    kfree(info);
    return res;
}
