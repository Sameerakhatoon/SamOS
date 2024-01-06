#include "file.h"
#include "config.h"
#include "memory/memory.h"
#include "memory/heap/kheap.h"
#include "status.h"
#include "kernel.h"
#include "fat/fat.h"
#include "string/string.h"
#include "disk/disk.h"
#include "lib/spinlock/spinlock.h"

// One lock over the descriptor table, the path parser
// and the filesystem drivers (FAT keeps one stream and its FAT cache
// per volume, none of it safe to share). Recursive: the elf loader and
// file_remove call several entry points under one hold.
static struct spinlock vfs_spinlock = SPINLOCK_INIT("vfs", LOCK_LEVEL_VFS, 1);

void vfs_lock(void){
    spin_lock(&vfs_spinlock);
}

void vfs_unlock(void){
    spin_unlock(&vfs_spinlock);
}

static struct filesystem**  fs_get_free_filesystem();
static void                 fs_static_load();
static void                 fs_load();
static int                  file_new_descriptor(struct file_descriptor** desc_out);
static struct file_descriptor* file_get_descriptor(int fd);

struct filesystem*       filesystems[SAMOS_MAX_FILESYSTEMS];
struct file_descriptor*  file_descriptors[SAMOS_MAX_FILE_DESCRIPTORS];

static struct filesystem** fs_get_free_filesystem(){
    int i = 0;
    for(i = 0; i < SAMOS_MAX_FILESYSTEMS; i++){
        if(filesystems[i] == 0){
            return &filesystems[i];
        }
    }
    return 0;
}

void fs_insert_filesystem(struct filesystem* filesystem){
    struct filesystem** fs;
    fs = fs_get_free_filesystem();
    if(!fs){
        print("Problem inserting filesystem");
        while(1){}
    }
    *fs = filesystem;
}

static void fs_static_load(){
    fs_insert_filesystem(fat_init());
}

static void fs_load(){
    memset(filesystems, 0, sizeof(filesystems));
    fs_static_load();
}

void fs_init(){
    memset(file_descriptors, 0, sizeof(file_descriptors));
    fs_load();
}

static int file_new_descriptor(struct file_descriptor** desc_out){
    int res = -ENOMEM;
    for(int i = 0; i < SAMOS_MAX_FILE_DESCRIPTORS; i++){
        if(file_descriptors[i] == 0){
            struct file_descriptor* desc = kzalloc(sizeof(struct file_descriptor));
            if(!desc){
                return -ENOMEM;
            }
            // Descriptors start at 1.
            desc->index = i + 1;
            file_descriptors[i] = desc;
            *desc_out = desc;
            res = 0;
            break;
        }
    }
    return res;
}

static struct file_descriptor* file_get_descriptor(int fd){
    if(fd <= 0 || fd > SAMOS_MAX_FILE_DESCRIPTORS){
        return 0;
    }
    int index = fd - 1;
    return file_descriptors[index];
}

struct filesystem* fs_resolve(struct disk* disk){
    struct filesystem* fs = 0;
    for(int i = 0; i < SAMOS_MAX_FILESYSTEMS; i++){
        if(filesystems[i] != 0 && filesystems[i]->resolve(disk) == 0){
            fs = filesystems[i];
            break;
        }
    }
    return fs;
}

static FILE_MODE file_get_mode_by_string(const char* str){
    FILE_MODE mode = FILE_MODE_INVALID;
    if(strncmp(str, "r", 1) == 0){
        mode = FILE_MODE_READ;
    } else if(strncmp(str, "w", 1) == 0){
        mode = FILE_MODE_WRITE;
    } else if(strncmp(str, "a", 1) == 0){
        mode = FILE_MODE_APPEND;
    }
    return mode;
}

// Parse a path and find its disk. On success the caller
// owns *root_out and must pathparser_free it.
static int file_resolve_path(const char* path, struct path_root** root_out, struct disk** disk_out){
    struct path_root* root_path = pathparser_parse(path, NULL);
    if(!root_path){
        return -EBADPATH;
    }
    struct disk* disk = disk_get(root_path->drive_no);
    if(!disk || !disk->filesystem){
        pathparser_free(root_path);
        return -EIO;
    }
    *root_out = root_path;
    *disk_out = disk;
    return 0;
}

// Fopen that says why it failed: a descriptor > 0 or
// a negative status. The fopen syscall returns this, so a program
// can tell -ENOENT from -EISDIR or -ERDONLY.
static int fopen_status_locked(const char* filename, const char* mode_str){
    int res = 0;
    struct path_root* root_path = NULL;
    struct disk* disk = NULL;
    void* descriptor_private_data = NULL;

    FILE_MODE mode = file_get_mode_by_string(mode_str);
    if(mode == FILE_MODE_INVALID){
        res = -EINVARG;
        goto out;
    }

    res = file_resolve_path(filename, &root_path, &disk);
    if(res < 0){
        goto out;
    }

    // A bare drive path like "0:/" is not a file.
    if(!root_path->first){
        res = -EINVARG;
        goto out;
    }

    descriptor_private_data = disk->filesystem->open(disk, root_path->first, mode);
    if(ISERR(descriptor_private_data)){
        res = ERROR_I(descriptor_private_data);
        goto out;
    }

    struct file_descriptor* desc = 0;
    res = file_new_descriptor(&desc);
    if(res < 0){
        disk->filesystem->close(descriptor_private_data);
        goto out;
    }

    desc->filesystem = disk->filesystem;
    desc->private    = descriptor_private_data;
    desc->disk       = disk;
    res = desc->index;

out:
    if(root_path){
        pathparser_free(root_path);
    }
    return res;
}

// The kernel's own callers test for 0 (unchanged API).
int fopen(const char* filename, const char* mode_str){
    int res = fopen_status(filename, mode_str);
    return res < 0 ? 0 : res;
}

static int fread_locked(void* ptr, uint32_t size, uint32_t nmemb, int fd){
    int res = 0;
    if(size == 0 || nmemb == 0 || fd < 1){
        res = -EINVARG;
        goto out;
    }

    struct file_descriptor* desc = file_get_descriptor(fd);
    if(!desc || desc->is_directory){
        res = -EINVARG;
        goto out;
    }

    res = desc->filesystem->read(desc->disk, desc->private, size, nmemb, (char*)ptr);

out:
    return res;
}

static int fseek_locked(int fd, int offset, FILE_SEEK_MODE whence){
    int res = 0;
    struct file_descriptor* desc = file_get_descriptor(fd);
    if(!desc || desc->is_directory){
        res = -EIO;
        goto out;
    }

    res = desc->filesystem->seek(desc->private, offset, whence);

out:
    return res;
}

static int fstat_locked(int fd, struct file_stat* stat){
    int res = 0;
    struct file_descriptor* desc = file_get_descriptor(fd);
    if(!desc){
        res = -EIO;
        goto out;
    }

    res = desc->filesystem->stat(desc->disk, desc->private, stat);

out:
    return res;
}

static void file_free_descriptor(struct file_descriptor* desc){
    file_descriptors[desc->index - 1] = 0x00;
    kfree(desc);
}

static int fclose_locked(int fd){
    int res = 0;
    struct file_descriptor* desc = file_get_descriptor(fd);
    if(!desc){
        res = -EIO;
        goto out;
    }

    res = desc->filesystem->close(desc->private);
    if(res == SAMOS_ALL_OK){
        file_free_descriptor(desc);
    }

out:
    return res;
}

// ---------------------------------------------------------------
// Write side of the VFS.
// ---------------------------------------------------------------

static int fwrite_locked(const void* ptr, uint32_t size, uint32_t nmemb, int fd){
    if(size == 0 || nmemb == 0 || !ptr){
        return -EINVARG;
    }
    struct file_descriptor* desc = file_get_descriptor(fd);
    if(!desc || desc->is_directory){
        return -EINVARG;
    }
    if(!desc->filesystem->write){
        return -ERDONLY;
    }
    return desc->filesystem->write(desc->disk, desc->private, size, nmemb, (const char*)ptr);
}

static int ftruncate_locked(int fd, uint32_t size){
    struct file_descriptor* desc = file_get_descriptor(fd);
    if(!desc || desc->is_directory){
        return -EINVARG;
    }
    if(!desc->filesystem->truncate){
        return -ERDONLY;
    }
    return desc->filesystem->truncate(desc->private, size);
}

static int fremove_locked(const char* path){
    struct path_root* root_path = NULL;
    struct disk* disk = NULL;
    int res = file_resolve_path(path, &root_path, &disk);
    if(res < 0){
        return res;
    }
    if(!root_path->first){
        res = -EINVARG;          // the root directory itself
    } else if(!disk->filesystem->unlink){
        res = -ERDONLY;
    } else {
        res = disk->filesystem->unlink(disk, root_path->first);
    }
    pathparser_free(root_path);
    return res;
}

static int fmkdir_locked(const char* path){
    struct path_root* root_path = NULL;
    struct disk* disk = NULL;
    int res = file_resolve_path(path, &root_path, &disk);
    if(res < 0){
        return res;
    }
    if(!root_path->first){
        res = -EEXIST;
    } else if(!disk->filesystem->mkdir){
        res = -ERDONLY;
    } else {
        res = disk->filesystem->mkdir(disk, root_path->first);
    }
    pathparser_free(root_path);
    return res;
}

static int fopendir_locked(const char* path){
    struct path_root* root_path = NULL;
    struct disk* disk = NULL;
    int res = file_resolve_path(path, &root_path, &disk);
    if(res < 0){
        return res;
    }
    if(!disk->filesystem->opendir){
        pathparser_free(root_path);
        return -EUNIMP;
    }
    void* private = disk->filesystem->opendir(disk, root_path->first);
    pathparser_free(root_path);
    if(ISERR(private)){
        return ERROR_I(private);
    }
    struct file_descriptor* desc = 0;
    res = file_new_descriptor(&desc);
    if(res < 0){
        disk->filesystem->close(private);
        return res;
    }
    desc->filesystem   = disk->filesystem;
    desc->private      = private;
    desc->disk         = disk;
    desc->is_directory = 1;
    return desc->index;
}

static int freaddir_locked(int fd, struct file_dirent* entry){
    struct file_descriptor* desc = file_get_descriptor(fd);
    if(!desc || !desc->is_directory || !entry){
        return -EINVARG;
    }
    return desc->filesystem->readdir(desc->private, entry);
}

int fopen_status(const char* filename, const char* mode_str){
    vfs_lock();
    int res = fopen_status_locked(filename, mode_str);
    vfs_unlock();
    return res;
}

int fread(void* ptr, uint32_t size, uint32_t nmemb, int fd){
    vfs_lock();
    int res = fread_locked(ptr, size, nmemb, fd);
    vfs_unlock();
    return res;
}

int fseek(int fd, int offset, FILE_SEEK_MODE whence){
    vfs_lock();
    int res = fseek_locked(fd, offset, whence);
    vfs_unlock();
    return res;
}

int fstat(int fd, struct file_stat* stat){
    vfs_lock();
    int res = fstat_locked(fd, stat);
    vfs_unlock();
    return res;
}

int fclose(int fd){
    vfs_lock();
    int res = fclose_locked(fd);
    vfs_unlock();
    return res;
}

int fwrite(const void* ptr, uint32_t size, uint32_t nmemb, int fd){
    vfs_lock();
    int res = fwrite_locked(ptr, size, nmemb, fd);
    vfs_unlock();
    return res;
}

int ftruncate(int fd, uint32_t size){
    vfs_lock();
    int res = ftruncate_locked(fd, size);
    vfs_unlock();
    return res;
}

int fremove(const char* path){
    vfs_lock();
    int res = fremove_locked(path);
    vfs_unlock();
    return res;
}

int fmkdir(const char* path){
    vfs_lock();
    int res = fmkdir_locked(path);
    vfs_unlock();
    return res;
}

int fopendir(const char* path){
    vfs_lock();
    int res = fopendir_locked(path);
    vfs_unlock();
    return res;
}

int freaddir(int fd, struct file_dirent* entry){
    vfs_lock();
    int res = freaddir_locked(fd, entry);
    vfs_unlock();
    return res;
}
