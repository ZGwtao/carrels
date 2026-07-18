
#include <carrels-monitor.h>
#include <monitor_vm_layout.h>


seL4_MessageInfo_t
monitor_call_backup_protocon_loading_context(microkit_channel ch)
{
    int cid = monitor_get_pcid_from_ch(ch);

    tsldr_context_t *context = \
        (tsldr_context_t *)monitor_vm_region_base(&monitor_vm_layout.loader_context, cid);

    tsldr_miscutil_memcpy(protocon_state_retrieve_context(cid), context, sizeof(tsldr_context_t));

    return microkit_msginfo_new(mon_NoError, 0);
}


void monitor_main_load_trustedlo(uint32_t cid)
{
    uintptr_t protocon_base =
            monitor_vm_region_base(&monitor_vm_layout.loader_program, cid);
    uintptr_t trampoline_base =
            monitor_vm_region_base(&monitor_vm_layout.trampoline_image, cid);
    uintptr_t payload_base =
            monitor_vm_region_base(&monitor_vm_layout.container_image, cid);

    size_t protocon_size = monitor_protocon_capacity();
    size_t trampoline_size = monitor_trampoline_capacity();

    tsldr_miscutil_memset((void *)protocon_base, 0, protocon_size);
    tsldr_miscutil_memset((void *)trampoline_base, 0, trampoline_size);

    tsldr_miscutil_load_elf(
        (void*)protocon_base,
        (const Elf64_Ehdr *)(__carrels_protocon_start)
    );
    tsldr_miscutil_memcpy(
        (void*)trampoline_base,
        (const char *)(__carrels_trampoline_start),
        trampoline_size
    );

    /* clean up client payload region entirely. */
    tsldr_miscutil_memset((void *)payload_base, 0, ORC_MONITOR_REGION_SIZE);
}

