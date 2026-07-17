#pragma once

#include <microkit.h>
#include <pcmcall/error.h>


#define MIN_REQ_PC_NUM 1U
#define MAX_REQ_PC_NUM 4U

typedef struct {
    uint32_t num_req_pc;
} monitor_deploy_request_t;


extern uint32_t req_pc_num;
extern bool deploy_request_active;
extern monitor_deploy_request_t deploy_request;
extern uintptr_t __carrels_payload_start;


seL4_MessageInfo_t
monitor_call_deploy_first_half(seL4_Word num_req_pc);


pc_monitor_Error protocon_deploy(payload_info_t *info);

