
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
            state->avail_service_per_type[type];

        if (service_index >= SVC_PER_TYPE_MAX_NUM) {
            TSLDR_DBG_PRINT(
                PROGNAME
                "Too many services of type %u for PC %zu\n",
                (unsigned int)type,
                pc_id
            );
            continue;
        }

        state->avail_service_refs[type][service_index] = service;
        state->avail_service_per_type[type] = service_index + 1;
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


void
service_registry_create(
    const monitor_svcdb_t *svcdb_list,
    pc_state_t *protocon_states,
    uint64_t pc_num
)
{
    assert(pc_num == svcdb_list->len);
    service_registry_validate_pc_count(pc_num);

    for (uint64_t pc_id = 0; pc_id < pc_num; ++pc_id) {
        service_registry_register_protocon_services(
            &svcdb_list->list[pc_id],
            pc_id,
            protocon_states
        );
    }
}