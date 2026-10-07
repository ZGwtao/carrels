/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <carrels-monitor.h>

#include <sddf/util/printf.h>

static inline void monitor_main_list_protocon_states(uint32_t num_protocons)
{
    if (num_protocons > PC_CHILD_PER_MONITOR_MAX_NUM) {
        TSLDR_DBG_PRINT(PROGNAME "Invalid number of protocons to list: %d\n", num_protocons);
        return;
    }
    for (uint32_t i = 0; i < num_protocons; ++i) {
        sddf_printf("[*] dynamic-PD [id=%d] has state: ", i);
        const protocon_lifecycle_state_t state = protocon_state_get_lifecycle_state(i);
        switch (state) {
        case PROTOCON_ACTIVE:
            sddf_printf("in-use");
            break;
        case PROTOCON_PASSIVE:
            sddf_printf("avail");
            break;
        case PROTOCON_SUSPENDED:
            sddf_printf("suspended");
            break;
        default:
            sddf_printf("unknown: %d", state);
        };
        sddf_printf("\n");
    }
}

seL4_MessageInfo_t monitor_call_list_protocons(void)
{
    monitor_main_list_protocon_states(ca_bootinfo.num_pc);

    return microkit_msginfo_new(MON_NO_ERROR, 0);
}

seL4_MessageInfo_t monitor_call_query_protocons(microkit_channel ch)
{
    monitor_main_list_protocon_states(ca_bootinfo.num_pc);

    microkit_channel self_id = monitor_get_pcid_from_ch(ch);
    seL4_Word bitmap = 0;
    for (int i = 0; i < PC_CHILD_PER_MONITOR_MAX_NUM; ++i) {
        if ((protocon_state_check_lifecycle_state(i, PROTOCON_ACTIVE) ||
             protocon_state_check_lifecycle_state(i, PROTOCON_SUSPENDED)) &&
            i != self_id) {
            bitmap |= (1ULL << i);
        }
    }
    seL4_MessageInfo_t ret = microkit_msginfo_new(MON_NO_ERROR, 2);
    microkit_mr_set(0, bitmap);
    microkit_mr_set(1, monitor_get_pcid_from_ch(ch));
    if (bitmap == 0) {
        sddf_printf("No dynamic PDs are currently available for communication\n");
    }
    return ret;
}
