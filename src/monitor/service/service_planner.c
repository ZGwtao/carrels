
#include <ossvc.h>
#include <protocon.h>
#include <assert.h>


static inline bool
service_planner_can_satisfy_request(
    const protocon_svc_req_t *req,
    const uint32_t avail_service_per_type[]
) {
    // if any requested service is more than whats offered
    // return false...

    for (int type = 0; type < SVC_TYPE_MAX_NUM; ++type) {
        if (req->service_count_per_type[type] >
            avail_service_per_type[type]) {
            return false;
        }
    }

    return true;
}

void
service_planner_select_protocon(
    const protocon_svc_req_t *req,
    deploy_plan_t *plan,
    const pc_state_t *protocon_states
) {
    deploy_plan_reset(plan);

    for (int pc_id = 0;
         pc_id < PC_CHILD_PER_MONITOR_MAX_NUM;
         ++pc_id) {
        if (!protocon_state_check_lifecycle_state(
                pc_id,
                PROTOCON_PASSIVE)) {
            continue;
        }

        if (!service_planner_can_satisfy_request(
                req,
                protocon_states[pc_id].avail_service_per_type)) {
            continue;
        }

        plan->pc_id = pc_id;
        break;
    }
    if (plan->pc_id >= PC_CHILD_PER_MONITOR_MAX_NUM) {
        TSLDR_DBG_PRINT(
            PROGNAME
            "No PC has sufficient services for the payload\n"
        );
        return;
    }
    plan->req = req;

    uint32_t cursor_service_num[SVC_TYPE_MAX_NUM];
    uint32_t *avail_service_num = protocon_states[plan->pc_id].avail_service_per_type;

    for (uint32_t i = 0; i < SVC_TYPE_MAX_NUM; ++i) {
        cursor_service_num[i] = avail_service_num[i];
    }

    for (uint32_t i = 0; i < req->service_count; ++i) {
        const protocon_svc_type_t type =
            req->service_entries[i]->type;

        const pc_state_t *state =
            &protocon_states[plan->pc_id];

        const uint32_t curr_service_index =
            --cursor_service_num[type];

        plan->service_sources[i] =
            state->avail_service_refs[type][curr_service_index];

        assert(plan->service_sources[i] != NULL);
    }
}