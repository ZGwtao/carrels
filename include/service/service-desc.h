#pragma once

#include <sel4/sel4.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    seL4_Word vaddr;
    seL4_Word page_num;
    seL4_Word page_size;
} svc_mapping_t;

typedef struct {
    // whether or not a valid os service
    bool svc_init;
    // corresponding to the XML gid
    uint8_t svc_idx;
    // the type of this os service
    uint8_t svc_type;
    // notifications
    uint8_t notifications[4];
    // ppcs
    uint8_t ppcs[4];
    // irqs
    uint8_t irqs[4];
    // ioports
    uint8_t ioports[4];
    // mappings
    svc_mapping_t mappings[4];
    // data_path
    char data_path[64];
} protocon_svc_t;

