/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <carrels-monitor.h>
#include <libtrustedlo.h>


void monitor_main_handle_fault(microkit_child child, microkit_msginfo msginfo)
{
    seL4_Word label = microkit_msginfo_get_label(msginfo);
    seL4_Word notify_flag = 0;

    if (label == seL4_Fault_VMFault) {
        seL4_Word ip = microkit_mr_get(seL4_VMFault_IP);
        seL4_Word address = microkit_mr_get(seL4_VMFault_Addr);
        seL4_Word prefetch_fault = microkit_mr_get(seL4_VMFault_PrefetchFault);
        seL4_Word fsr = microkit_mr_get(seL4_VMFault_FSR);

        notify_flag = ip | address;

        if (notify_flag) {
            TSLDR_DBG_PRINT(PROGNAME "seL4_Fault_VMFault\n");
            TSLDR_DBG_PRINT(PROGNAME "child: %d\n", child);
            TSLDR_DBG_PRINT(PROGNAME "Fault address: 0x%llx\n", (unsigned long long)address);
            TSLDR_DBG_PRINT(PROGNAME "Fault instruction pointer: 0x%llx\n", (unsigned long long)ip);
            TSLDR_DBG_PRINT(PROGNAME "Prefetch fault: %llu\n", (unsigned long long)prefetch_fault);
            TSLDR_DBG_PRINT(PROGNAME "FSR: 0x%llx\n", (unsigned long long)fsr);
            TSLDR_DBG_PRINT(PROGNAME "Fault type: %s\n", prefetch_fault ? "instruction fetch" : "data access");

            for (seL4_Word i = 0; i < 8; i++) {
                TSLDR_DBG_PRINT(PROGNAME "MR[%llu]: 0x%llx\n", (unsigned long long)i, (unsigned long long)microkit_mr_get(i));
            }
        } else {
            TSLDR_DBG_PRINT(PROGNAME "receive the first fault from an empty pd with id: '%d'\n", child);
        }
    }
    microkit_pd_stop(child);

    if (!notify_flag) {
        return;
    }

    TSLDR_DBG_PRINT(PROGNAME "Fault label: %llu\n", (unsigned long long)label);
    monitor_main_notify_orchestrator();

    monitor_call_restore_protocon(child + PC_MONITOR_PROTOCON_BASE_CHANNEL);
}
