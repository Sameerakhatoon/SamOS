// Lecture 184 - disk-driver registry. Stores known drivers in a
// vector, dispatches mount + mount_partition through the vtable.

#include "driver.h"
#include "lib/vector/vector.h"
#include "disk/disk.h"
#include "string/string.h"
#include "memory/memory.h"
#include "status.h"

struct vector* disk_driver_vec = NULL;

int disk_driver_system_load_drivers(){
    int res = 0;
    // Register ATA/IDE PATA. (L189 wires it.)
    // Register NVME. (L195 wires it.)
    return res;
}

int disk_driver_system_init(){
    int res = 0;
    disk_driver_vec = vector_new(sizeof(struct disk_driver*), 8, 0);
    if(!disk_driver_vec){
        res = -ENOMEM;
        goto out;
    }
    res = disk_driver_system_load_drivers();
    if(res < 0){
        goto out;
    }
out:
    return res;
}

int disk_driver_mount_partition(struct disk_driver* driver, struct disk* disk,
                                int starting_lba, int ending_lba,
                                struct disk** partition_disk_out){
    int res = 0;
    if(driver != disk->driver || disk->driver == NULL){
        res = -EINVARG;
        goto out;
    }
    if(disk->type != SAMOS_DISK_TYPE_REAL){
        res = -EUNIMP;
        goto out;
    }
    res = disk->driver->functions.mount_partition(disk, starting_lba, ending_lba, partition_disk_out);
out:
    return res;
}

bool disk_driver_registered(struct disk_driver* driver){
    size_t total_drivers = vector_count(disk_driver_vec);
    for(size_t i = 0; i < total_drivers; i++){
        struct disk_driver* current_driver = NULL;
        vector_at(disk_driver_vec, i, &current_driver, sizeof(current_driver));
        if(current_driver &&
           strncmp(current_driver->name, driver->name, sizeof(current_driver->name)) == 0){
            return true;
        }
    }
    return false;
}

void* disk_driver_private_data(struct disk_driver* driver){
    return driver->private;
}

// Lecture 184 - register a driver. Upstream calls
// `disk_driver_register(driver)` recursively inside the early
// uniqueness check, where the intent was clearly
// `disk_driver_registered(driver)`. As written the function
// infinite-recurses on the very first call and overflows the
// stack. We preserve the typo'd call verbatim per the project
// rule; at L184 nothing actually invokes this path (the load
// helper is an empty stub) so the bug stays dormant until a
// later lecture (or a future fix commit) wires real drivers.
int disk_driver_register(struct disk_driver* driver){
    int res = 0;
    if(disk_driver_register(driver)){
        res = -EISTKN;
        goto out;
    }
    vector_push(disk_driver_vec, &driver);
    if(driver->functions.loaded){
        res = driver->functions.loaded(driver);
        if(res < 0){
            vector_pop_element(disk_driver_vec, &driver, sizeof(driver));
            goto out;
        }
    }
out:
    return res;
}

int disk_driver_mount_all(){
    size_t total_drivers = vector_count(disk_driver_vec);
    for(size_t i = 0; i < total_drivers; i++){
        struct disk_driver* driver = NULL;
        vector_at(disk_driver_vec, i, &driver, sizeof(driver));
        if(driver && driver->functions.mount){
            driver->functions.mount(driver);
        }
    }
    return 0;
}
