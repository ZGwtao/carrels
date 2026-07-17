#pragma once

#define SVC_TYPE_MAX_NUM (8)
#define PC_CHILD_PER_MONITOR_MAX_NUM (16)
#define SVC_PER_TYPE_MAX_NUM (8)

#define PROGNAME "[@monitor] "

#include <service/service-desc.h>
#include <service/manifest.h>
#include <service/registry.h>

#include <libtrustedlo.h>

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



seL4_Error service_manifest_header_parse(payload_info_t *info, uintptr_t base);


void service_manifest_parse(payload_info_t *payload, protocon_svc_req_t *req);



void service_registry_create(const monitor_svcdb_t *svcdb_list, pc_state_t *protocon_states);


void service_planner_select_protocon(
    const protocon_svc_req_t *req,
    deploy_plan_t *plan,
    const pc_state_t *protocon_states
);


void service_installer_apply(const deploy_plan_t *plan);
