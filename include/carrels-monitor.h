#pragma once

#include <libtrustedlo.h>
#include <carrels-services.h>
#include <carrels-user.h>

#include <monitor/mcall.h>
#include <monitor/fault.h>
#include <monitor/payload.h>


#define PROGNAME "[@monitor] "

#define ORC_MONITOR_REGION_SIZE (0x800000)



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

typedef uint8_t protocon_lifecycle_state_t;
enum {
    PROTOCON_ACTIVE = 1,
    PROTOCON_PASSIVE,
    PROTOCON_HANG,
};
_Static_assert(sizeof(protocon_lifecycle_state_t) == sizeof(uint8_t),
               "protocon_lifecycle_state_t must be uint8_t");

typedef struct {
    uint32_t pc_id;
    uintptr_t pc_base;
    Elf64_Addr pc_entry;
    const protocon_svc_req_t *req;
    const protocon_svc_t *service_sources[16];
    uintptr_t base_serialised_service;
} deploy_plan_t;


static inline void
deploy_plan_memzero_services(deploy_plan_t *p)
{
    tsldr_miscutil_memset(
        p->service_sources,
        0,
        sizeof(protocon_svc_t *)
    );
}

static inline
void deploy_plan_reset(deploy_plan_t *p)
{
    p->pc_id = PC_CHILD_PER_MONITOR_MAX_NUM;
    p->pc_base = 0x0;
    p->pc_entry = 0x0;
    p->req = NULL;
    deploy_plan_memzero_services(p);
    p->base_serialised_service = 0x0;
}


typedef struct pc_state {
    uint32_t pc_id;
    tsldr_context_t context;
    protocon_lifecycle_state_t life_cycle_state;
    uint32_t avail_service_per_type[SVC_TYPE_MAX_NUM];
    const protocon_svc_t *avail_service_refs[SVC_TYPE_MAX_NUM][SVC_PER_TYPE_MAX_NUM];
} pc_state_t;


extern pc_state_t protocon_states[PC_CHILD_PER_MONITOR_MAX_NUM];


static inline tsldr_context_t *
protocon_state_retrieve_context(uint32_t pc_id)
{
    if (pc_id >= PC_CHILD_PER_MONITOR_MAX_NUM) {
        TSLDR_DBG_PRINT(
            PROGNAME
            "Invalid cid given for retrieving context from protocon_states\n"
        );
        return NULL;
    }
    return &(protocon_states[pc_id].context);
}

static inline void
protocon_state_set_lifecycle_state(
    uint32_t pc_id,
    protocon_lifecycle_state_t state
) {
    protocon_states[pc_id].life_cycle_state = state;
}

static inline protocon_lifecycle_state_t
protocon_state_get_lifecycle_state(uint32_t pc_id)
{
    return protocon_states[pc_id].life_cycle_state;
}

static inline bool
protocon_state_check_lifecycle_state(
    uint32_t pc_id,
    protocon_lifecycle_state_t state
) {
    return protocon_state_get_lifecycle_state(pc_id) == state;
}

static inline void
protocon_state_memzero_services(uint32_t pc_id)
{
    pc_state_t *state = &protocon_states[pc_id];

    for (uint32_t i = 0; i < SVC_TYPE_MAX_NUM; ++i) {
        tsldr_miscutil_memset(
            state->avail_service_refs[i],
            0,
            (SVC_PER_TYPE_MAX_NUM) * sizeof(protocon_svc_t *)
        );
        state->avail_service_per_type[i] = 0;
    }
}

static inline void
protocon_state_memzero_context(uint32_t pc_id)
{
    pc_state_t *state = &protocon_states[pc_id];
    tsldr_miscutil_memset(
        &state->context,
        0,
        sizeof(tsldr_context_t)
    );
}


#define SET_PROTOCON_AS_INSTANTIATED(C) \
    do { protocon_state_set_lifecycle_state(C, PROTOCON_ACTIVE); } while (0);

#define SET_PROTOCON_AS_HANG(C) \
    do { protocon_state_set_lifecycle_state(C, PROTOCON_HANG); } while (0);

#define SET_PROTOCON_AS_AVAILABLE(C) \
    do { protocon_state_set_lifecycle_state(C, PROTOCON_PASSIVE); } while (0);

#define PC_MONITOR_ORCHESTRATOR_CHANNEL (15)
#define PC_MONITOR_PROTOCON_BASE_CHANNEL (24)

#ifndef PC_CHILD_PER_MONITOR_MAX_NUM
#define PC_CHILD_PER_MONITOR_MAX_NUM (16)
#endif

#define INVALID_PC_ID (0xffc)

static inline void monitor_main_notify_orchestrator()
{
    microkit_notify(PC_MONITOR_ORCHESTRATOR_CHANNEL);
}

static inline int
monitor_main_get_cid_from_channel(microkit_channel ch)
{
    if (ch < PC_MONITOR_PROTOCON_BASE_CHANNEL ||
        ch >= (PC_MONITOR_PROTOCON_BASE_CHANNEL + PC_CHILD_PER_MONITOR_MAX_NUM))
    {
        return (INVALID_PC_ID);
    }
    return ch - PC_MONITOR_PROTOCON_BASE_CHANNEL;
}


seL4_Error service_manifest_header_parse(payload_info_t *info, uintptr_t base);


void service_manifest_parse(payload_info_t *payload, protocon_svc_req_t *req);



void service_registry_create(const monitor_svcdb_t *svcdb_list, pc_state_t *protocon_states);


void service_planner_select_protocon(
    const protocon_svc_req_t *req,
    deploy_plan_t *plan,
    const pc_state_t *protocon_states
);


void service_installer_apply(const deploy_plan_t *plan);
