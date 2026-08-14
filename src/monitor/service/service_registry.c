/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <carrels-monitor.h>
#include <assert.h>


static inline void
service_registry_register_protocon_services(
    const protocon_svcdb_t *services,
    uint32_t pc_id,
    pc_state_t *protocon_states
)
{
    pc_state_t *state = &protocon_states[pc_id];

    for (uint32_t i = 0; i < services->svc_num; ++i) {
        const protocon_svc_t *service = &services->array[i];

        if (service->svc_init == false) {
            continue;
        }

        const protocon_svc_type_t type = service->svc_type;
        if (type >= SVC_TYPE_MAX_NUM) {
            TSLDR_DBG_PRINT(
                PROGNAME "Invalid service type: %d\n",
                type
            );
            continue;
        }

        const uint32_t service_index =
            state->resource_quota.avail_service_per_type[type];

        if (service_index >= SVC_PER_TYPE_MAX_NUM) {
            TSLDR_DBG_PRINT(
                PROGNAME
                "Too many services of type %u for PC %zu\n",
                (unsigned int)type,
                pc_id
            );
            continue;
        }

        state->resource_quota.avail_service_refs[type][service_index] = NULL;
        state->resource_quota.avail_service_per_type[type] = service_index + 1;
    }
}


static inline void
service_registry_validate_pc_count(size_t pc_count)
{
    if (pc_count > PC_CHILD_PER_MONITOR_MAX_NUM) {
        TSLDR_DBG_PRINT(
            PROGNAME
            "Invalid PC count: %d; maximum supported count is %d\n",
            pc_count,
            (PC_CHILD_PER_MONITOR_MAX_NUM)
        );
        microkit_internal_crash(-1);
    }

    TSLDR_DBG_PRINT(
        PROGNAME
        "Number of available PCs recorded from svcdb: %d\n",
        pc_count
    );
}

// typedef struct {
//     uint16_t version;
//     uint32_t service_count;
//     uint32_t total_size;
//     const svc_service_t *services[SVC_MAX_SERVICES];
// } svc_t;

void service_registry_create(const svc_t *svcdb_list, pc_state_t *protocon_states, uint64_t pc_num)
{
    if (!svcdb_list) return;

    for (uint32_t i = 0; i < svcdb_list->service_count; ++i) {
        const svc_service_t *service = svcdb_list->services[i];
        protocon_svc_type_t type = service->service_type;

        if (type >= SVC_TYPE_MAX_NUM) {
            TSLDR_DBG_PRINT(PROGNAME "Invalid service type: %d\n", type);
            continue;
        }
        // FIXME
        // should not use the PD id as index of protocon_states
        // (recalculate the actual index using the info retrieved from "dlg")
        //
        uint64_t pc_id = service->pd_id;
        pc_state_t *state = &protocon_states[pc_id];
        // FIXME
        // we can use a function to wrap up the ID alloc
        // for each service type available to a protocon
        const uint32_t idx = state->resource_quota.avail_service_per_type[type];

        if (idx >= SVC_PER_TYPE_MAX_NUM) {
            TSLDR_DBG_PRINT(PROGNAME "Too many services of type %d for PC %lu\n", type, pc_id);
            continue;
        }

        state->resource_quota.avail_service_refs[type][idx] = service;
        state->resource_quota.avail_service_per_type[type] = idx + 1;
    }
}