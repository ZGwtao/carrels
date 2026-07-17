
#include <carrels-monitor.h>
#include <libtrustedlo.h>


seL4_MessageInfo_t
monitor_main_handle_pccall(microkit_channel ch)
{
    /* get the first word of the message */
    seL4_Word call_id = microkit_mr_get(0);
    seL4_MessageInfo_t ret = microkit_msginfo_new(0, 0);

    /* call for the container monitor */
    switch (call_id) {
    case PC_MONITOR_CALL_DEPLOY:
        TSLDR_DBG_PRINT(PROGNAME "Deploy an application to a dynamic PD\n");
        seL4_Word num_req_pc = microkit_mr_get(1);
        ret = monitor_call_deploy_first_half(num_req_pc);
        break;
    case PC_MONITOR_CALL_FLIP_ACL_RULE:
        pd_io_acl_rule = !pd_io_acl_rule;
        break;
    case PC_MONITOR_CALL_BACKUP_CONTEXT:
        TSLDR_DBG_PRINT(PROGNAME "Backing up trusted loading context for dynamic PD with ID: %d\n", monitor_main_get_cid_from_channel(ch));
        ret = monitor_call_backup_protocon_loading_context(ch);
        break;
    case PC_MONITOR_CALL_TERMINATE:
        TSLDR_DBG_PRINT(PROGNAME "Exit and uninstantiate a dynamic PD\n");
        ret = monitor_call_stop_and_restore_protocon(ch - PC_MONITOR_PROTOCON_BASE_CHANNEL);
        break;
    case PC_MONITOR_CALL_HANG: {
        seL4_Word target_pd_id = seL4_GetMR(1);
        TSLDR_DBG_PRINT(PROGNAME "Hang dynamic PD with ID: %d\n", target_pd_id);
        ret = monitor_call_hang_protocon(target_pd_id);
        break;
    }
    case PC_MONITOR_CALL_RESUME: {
        seL4_Word target_pd_id = seL4_GetMR(1);
        TSLDR_DBG_PRINT(PROGNAME "Resume dynamic PD with ID: %d\n", target_pd_id);
        ret = monitor_call_resume_protocon(target_pd_id);
        break;
    }
    case PC_MONITOR_CALL_LIST_PROTOCONS:
        TSLDR_DBG_PRINT(PROGNAME "List states of dynamic PDs\n");
        ret = monitor_call_list_protocons();
        break;
    case PC_MONITOR_CALL_QUERY_PROTOCONS:
        TSLDR_DBG_PRINT(PROGNAME "Query states of dynamic PDs\n");
        ret = monitor_call_query_protocons(ch);
        break;
    case PC_MONITOR_CALL_TERMINATE_EXT: {
        seL4_Word target_pd_id = seL4_GetMR(1);
        if (ch >= 24) {
            target_pd_id = monitor_main_get_cid_from_channel(ch);
        }
        TSLDR_DBG_PRINT(PROGNAME "Terminate dynamic PD with ID: %d\n", target_pd_id);
        ret = monitor_call_stop_and_restore_protocon(target_pd_id);
        break;
    }
    default:
        /* do nothing for now */
        TSLDR_DBG_PRINT(PROGNAME "Undefined container monitor call: %d\n", call_id);
        break;
    }

    return ret;
}
