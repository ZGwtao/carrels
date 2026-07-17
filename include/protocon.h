
#pragma once

// maximum 8 os services instances per OS service type
#define PC_SVC_PER_PD_MAX_NUM (8)
// maximum 8 os service types
#define PC_SVC_TYPE_MAX_NUM (8)

typedef int protocon_svc_type_t;
enum {
    SERVICE_DUMMY = -1,
    SERVICE_FILE_SYSTEM = 0,
    SERVICE_DEVICE_SERIAL,
    SERVICE_DEVICE_ETHERNET,
    SERVICE_DEVICE_TIMER,
    SERVICE_DEVICE_I2C,
    SERVICE_RESERVED,
};
_Static_assert(sizeof(protocon_svc_type_t) == sizeof(int),
               "protocon_svc_type_t must be int");


#define PC_CHILD_PER_MONITOR_MAX_NUM (16)
