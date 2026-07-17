#pragma once

#include <service/service-desc.h>


typedef struct {
    // specify which PD this array belongs to
    uint8_t pd_idx;
    // number of available os services in the array
    uint8_t svc_num;
    // array of os services of this PD
    protocon_svc_t array[16];
} protocon_svcdb_t;

typedef struct {
    // overall length of this region
    uint64_t len;
    // list of os service arrays
    protocon_svcdb_t list[16];
} monitor_svcdb_t;
