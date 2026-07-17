
#include <mcall.h>

#include <sddf/util/printf.h>


static inline void
monitor_main_list_protocon_states(uint32_t num_protocons)
{
    if (num_protocons > PC_CHILD_PER_MONITOR_MAX_NUM) {
        TSLDR_DBG_PRINT(
            PROGNAME
            "Invalid number of protocons to list: %d\n",
            num_protocons
        );
        return;
    }
    for (uint32_t i = 0; i < num_protocons; ++i) {
        sddf_printf("[*] dynamic-PD [id=%d] has state: ", i);
        const protocon_lifecycle_state_t state = 
                protocon_state_get_lifecycle_state(i);
        switch (state) {
            case PROTOCON_ACTIVE:
                sddf_printf("in-use");
                break;
            case PROTOCON_PASSIVE:
                sddf_printf("avail");
                break;
            case PROTOCON_HANG:
                sddf_printf("hang");
                break;
            default:
                sddf_printf("unknown: %d", state);
        };
        sddf_printf("\n");
    }
}


seL4_MessageInfo_t
monitor_call_list_protocons(void)
{
    // FIXME: should not hardcode the number of pc to list
    monitor_main_list_protocon_states(4);
    return microkit_msginfo_new(mon_NoError, 0);
}


seL4_MessageInfo_t
monitor_call_query_protocons(microkit_channel ch)
{
    // FIXME: should not hardcode the number of pc to list
    monitor_main_list_protocon_states(4);

    seL4_Word self_id = monitor_main_get_cid_from_channel(ch);
    seL4_Word bitmap = 0;
    for (int i = 0; i < PC_CHILD_PER_MONITOR_MAX_NUM; ++i) {
        if ((protocon_state_check_lifecycle_state(i, PROTOCON_ACTIVE) ||
             protocon_state_check_lifecycle_state(i, PROTOCON_HANG)) && i != self_id) {
            bitmap |= (1ULL << i);
        }
    }
    seL4_MessageInfo_t ret = microkit_msginfo_new(mon_NoError, 2);
    microkit_mr_set(0, bitmap);
    microkit_mr_set(1, monitor_main_get_cid_from_channel(ch));
    if (bitmap == 0) {
        sddf_printf("No dynamic PDs are currently available for communication\n");
    }
    return ret;
}
