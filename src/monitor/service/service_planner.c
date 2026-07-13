
#include <ossvc.h>
#include <protocon.h>


static inline
seL4_Word monitor_match_ossvc_request_with_unipd(
        const protocon_svc_req_t *req,
        int svc_dist_map[]
) {
    seL4_Word mask = 0;
    for (int i = 0; i < SVC_TYPE_MAX_NUM; ++i) {
        mask |= (req->num_svc_per_type[i] > svc_dist_map[i]);
    }
    // if mask is zero, it means all requested OS services are no less than what have been provided
    // so that means we have a match between a dynamic PD and a set of OS services request
    return mask;
}

void service_planner_select_protocon(
        const protocon_svc_req_t *req,
        deploy_plan_t *plan,
        protocon_lifecycle_state_t *protocon_states,
        int monitor_svc_dist_map[][SVC_TYPE_MAX_NUM]
) {

    deploy_plan_init(plan);

    for (int i = 0; i < PC_CHILD_PER_MONITOR_MAX_NUM; ++i) {
        if (protocon_states[i] != PROTOCON_PASSIVE) {
            continue;
        }
        // check each dynamic pd and see if any of them matches with the OS service request
        seL4_Word mask = monitor_match_ossvc_request_with_unipd(req, monitor_svc_dist_map[i]);
        if (mask == 0) {
            plan->pc_id = i;
        }
    }
}
