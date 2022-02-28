// Lecture 184 - disk-driver registry. Stores known drivers in a
// vector, dispatches mount + mount_partition through the vtable.

#include "driver.h"
#include "lib/vector/vector.h"
#include "disk/disk.h"
#include "string/string.h"
#include "memory/memory.h"
#include "drivers/pata.h"      // L188 - pata_driver_init
#include "drivers/nvme.h"      // L195 - nvme_driver_init
#include "status.h"

struct vector* disk_driver_vec = NULL;

int disk_driver_system_load_drivers(){
    int res = 0;
    // Lecture 188 - register the PATA driver.
    res = disk_driver_register(pata_driver_init());
    if(res < 0){
        goto out;
    }
    // Lecture 195 - register the NVME driver.
    res = disk_driver_register(nvme_driver_init());
out:
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

// Lecture 184 / 189 - register a driver. L184 wrote
// `disk_driver_register(driver)` in the uniqueness check
// (self-recursion). L189 fixes it to `disk_driver_registered`.
// SamOs follows the L189 fix here.
int disk_driver_register(struct disk_driver* driver){
    int res = 0;
    if(disk_driver_registered(driver)){
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
