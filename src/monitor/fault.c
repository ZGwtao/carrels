
#include <fault.h>
#include <ossvc.h>
#include <monitor.h>
#include <libtrustedlo.h>


void monitor_main_handle_fault(microkit_child child, microkit_msginfo msginfo)
{
    seL4_Word label = microkit_msginfo_get_label(msginfo);
    seL4_Word notidy_flag = 0;
    // we handle only VM fault particularly...
    if (label == seL4_Fault_VMFault) {
        seL4_Word ip = microkit_mr_get(seL4_VMFault_IP);
        seL4_Word address = microkit_mr_get(seL4_VMFault_Addr);
        // we use it for running into a purely empty thread? (or init a dynamic pd)
        notidy_flag = ip | address;
        if (notidy_flag) {
            TSLDR_DBG_PRINT(PROGNAME "seL4_Fault_VMFault\n");
            TSLDR_DBG_PRINT(PROGNAME "Fault address: 0x%x\n", (unsigned long long)address);
            TSLDR_DBG_PRINT(PROGNAME "Fault instruction pointer: 0x%x\n", (unsigned long long)ip);
        } else {
            TSLDR_DBG_PRINT(PROGNAME "receive the first fault from an empty pd with id; '%d'\n", child);
        }
    }
    microkit_pd_stop(child);

    if (!notidy_flag) {
        // early return if we receive the first fault from dynamic pds (or empty threads if possible?)
        return;
    }
    // do not print fault label for initialising dynamic pd...
    TSLDR_DBG_PRINT(PROGNAME "Fault label: %d\n", label);
    monitor_main_notify_orchestrator();

    monitor_call_restore_protocon(child + PC_MONITOR_PROTOCON_BASE_CHANNEL);
}
