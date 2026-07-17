#pragma once

#include <microkit.h>
#include <monitor.h>

#define MIN_REQ_PC_NUM 1U
#define MAX_REQ_PC_NUM 4U

typedef struct {
    uint32_t num_req_pc;
} monitor_deploy_request_t;


extern uint32_t req_pc_num;
extern bool deploy_request_active;
extern monitor_deploy_request_t deploy_request;
extern uintptr_t __carrels_payload_start;


static inline void
monitor_deploy_refresh_request(void)
{
    req_pc_num = 0;
    deploy_request.num_req_pc = 0;
    deploy_request_active = false;
}

static inline void
monitor_finish_deploy_request(void)
{
    monitor_deploy_refresh_request();
    monitor_main_notify_orchestrator();
}


seL4_MessageInfo_t
monitor_call_deploy_first_half(seL4_Word num_req_pc);


pc_monitor_Error protocon_deploy(payload_info_t *info);


void protocon_pre_instantiate(deploy_plan_t *plan, const payload_info_t *payload);


void protocon_start(deploy_plan_t *plan);

