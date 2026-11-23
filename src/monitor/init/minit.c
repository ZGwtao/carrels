/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <carrels-monitor.h>
#include <libmicrokitco.h>
#include <assert.h>
#include <sddf/util/printf.h>

dlg_header_t dlg;
svc_t svc;

uintptr_t svc_buffer_vaddr;
uintptr_t dlg_buffer_vaddr;
uintptr_t sym_buffer_vaddr;
uintptr_t sym_buffer_size;

static const char *ca_monitor_find_terminator(const char *text, size_t capacity)
{
    for (size_t i = 0; i < capacity; ++i) {
        if (text[i] == '\0') {
            return text + i;
        }
    }
    return NULL;
}

static void ca_monitor_make_metadata_filepath(char *filepath, size_t capacity, const char *suffix)
{
    const char *name_end = ca_monitor_find_terminator(microkit_name, sizeof(microkit_name));
    if (name_end == NULL) {
        TSLDR_DBG_PRINT(PROGNAME "Microkit name is not terminated\n");
        microkit_internal_crash(-1);
    }

    size_t name_len = (size_t)(name_end - microkit_name);
    if (capacity <= name_len + 1) {
        TSLDR_DBG_PRINT(PROGNAME "Metadata filename is too long\n");
        microkit_internal_crash(-1);
    }

    size_t suffix_space = capacity - name_len - 1;
    if (ca_monitor_find_terminator(suffix, suffix_space) == NULL) {
        TSLDR_DBG_PRINT(PROGNAME "Metadata filename is too long\n");
        microkit_internal_crash(-1);
    }

    int len = sddf_snprintf(filepath, capacity, "%s.%s", microkit_name, suffix);
    if (len < 0 || (size_t)len >= capacity) {
        TSLDR_DBG_PRINT(PROGNAME "Metadata filename is too long\n");
        microkit_internal_crash(-1);
    }
}

static void ca_monitor_load_metadata(const char *suffix, void *buffer)
{
    char fpath[MICROKIT_PD_NAME_LENGTH + 10];
    ca_monitor_make_metadata_filepath(fpath, sizeof(fpath), suffix);

    int err;
    pico_vfs_readfile2buf(buffer, fpath, &err);
    if (err != seL4_NoError) {
        TSLDR_DBG_PRINT(PROGNAME "Failed to load %s\n", fpath);
        microkit_internal_crash(-1);
    }
}

static void ca_monitor_load_and_parse_delegation_info(void *buffer, dlg_header_t *delegation_info)
{
    ca_monitor_load_metadata("dlg", buffer);
    if (!dlg_parse(buffer, delegation_info)) {
        TSLDR_DBG_PRINT(PROGNAME "failed to parse dlg\n");
        while (1) {
            ;
        }
    }
    // for (uint32_t i = 0; i < delegation_info->delegator_count; i++) {
    //     const dlg_delegator_t *delegator = delegation_info->delegators[i];

    //     TSLDR_DBG_PRINT("pd=%d cap=%d resources=%d\n",
    //                     delegator->pd_id,
    //                     delegator->delegation_cap,
    //                     delegator->resource_count);
    // }
}

static void ca_monitor_load_and_parse_service_info(void *buffer, svc_t *service_info)
{
    ca_monitor_load_metadata("svc", buffer);
    if (!svc_parse(buffer, service_info)) {
        TSLDR_DBG_PRINT(PROGNAME "failed to parse svc\n");
        while (1) {
            ;
        }
    }

    // for (uint32_t i = 0; i < service_info->service_count; i++) {
    //     const svc_service_t *service = service_info->services[i];

    //     TSLDR_DBG_PRINT("pd=%d id=%d type=%d resources=%d path=%.*s\n",
    //                     service->pd_id,
    //                     service->service_id,
    //                     service->service_type,
    //                     service->resource_count,
    //                     (int)service->path_len,
    //                     svc_service_path(service));

    //     for (uint32_t j = 0; j < service->resource_count; j++) {
    //         const svc_resource_t *resource = svc_service_resource(service, j);
    //         TSLDR_DBG_PRINT("  kind=%d value=0x%x\n", resource->kind, resource->value);
    //     }
    // }
}

static void ca_monitor_init_cothread_spawn(const client_entry_t client_entry,
                                          void *arg,
                                          const char *err_msg)
{
    if (microkit_cothread_spawn(client_entry, arg) == LIBMICROKITCO_NULL_HANDLE) {
        TSLDR_DBG_PRINT("%s", err_msg);
        microkit_internal_crash(-1);
    }
}

static inline void protocon_state_init_symb_header(uint32_t pc_id)
{
    int err;
    char sym_file[80];
    uintptr_t slot_size = sym_buffer_size / PC_CHILD_PER_MONITOR_MAX_NUM;
    uintptr_t base = sym_buffer_vaddr + pc_id * slot_size;
    pc_state_t *state = &protocon_states[pc_id];

    sddf_snprintf(sym_file, sizeof(sym_file), "protocon%u.mktsym", pc_id);

    pico_vfs_readfile2buf((void *)base, sym_file, &err);
    if (err != seL4_NoError) {
        TSLDR_DBG_PRINT(PROGNAME "Failed to load %s\n", sym_file);
        microkit_internal_crash(-1);
    }

    mktsymb_read(&state->sym_header, (void *)base);
}

static inline pc_monitor_error ca_monitor_init_validate_pc_count(uint32_t pc_count)
{
    if (pc_count > PC_CHILD_PER_MONITOR_MAX_NUM) {
        return MON_INVALID_REQ_PC_NUM;
    }

    TSLDR_DBG_PRINT(PROGNAME "Number of available PCs recorded from svcdb: %d\n", pc_count);
    return MON_NO_ERROR;
}

static inline void ca_monitor_init_get_pcnum(uint32_t delegator_cnt, ca_monitor_bootinfo_t *info)
{
    if (ca_monitor_init_validate_pc_count(delegator_cnt) != MON_NO_ERROR) {
        TSLDR_DBG_PRINT(PROGNAME "Invalid PC count: %d; maximum supported count is %d\n",
                        delegator_cnt,
                        (PC_CHILD_PER_MONITOR_MAX_NUM));
        while (MON_INVALID_REQ_PC_NUM) {
            ;
        }
    }
    info->num_pc = delegator_cnt;
    assert(info->num_pc <= PC_CHILD_PER_MONITOR_MAX_NUM);
}

static inline void ca_monitor_init_protocon_states(uint64_t pc_num)
{
    for (uint64_t i = 0; i < pc_num; ++i) {
        protocon_states[i].pc_id = i;
        protocon_state_memzero_services(i);
        protocon_state_memzero_context(i);
        protocon_state_memzero_symb_header(i);
        protocon_state_init_symb_header(i);
        monitor_main_load_trustedlo(i);
        SET_PROTOCON_AS_AVAILABLE(i);
    }

    service_registry_create(&svc, protocon_states, pc_num);
}

static void ca_monitor_boot_phase_first(void)
{
    TSLDR_DBG_PRINT(PROGNAME "Boot phase first: mounting storage\n");
    fs_cmpl_t completion;
    int err = fs_command_blocking(&completion, (fs_cmd_t){.type = FS_CMD_INITIALISE});

    if (err || completion.status != FS_STATUS_SUCCESS) {
        TSLDR_DBG_PRINT(PROGNAME "Failed to mount\n");
        microkit_internal_crash(-1);
    }

    TSLDR_DBG_PRINT(PROGNAME "Boot phase first: storage ready\n");
}

static void ca_monitor_boot_phase_second(void)
{
    TSLDR_DBG_PRINT(PROGNAME "Boot phase second: loading metadata\n");
    ca_monitor_load_and_parse_delegation_info((void *)dlg_buffer_vaddr, &dlg);
    ca_monitor_load_and_parse_service_info((void *)svc_buffer_vaddr, &svc);

    TSLDR_DBG_PRINT(PROGNAME "Boot phase second: metadata ready\n");
}

static void ca_monitor_boot_phase_third(void)
{
    TSLDR_DBG_PRINT(PROGNAME "Boot phase third: initialising states\n");
    void *binfo = microkit_cothread_my_arg();

    assert(binfo);
    ca_monitor_bootinfo_t *bootinfo = (ca_monitor_bootinfo_t *)binfo;

    ca_monitor_init_get_pcnum(dlg.delegator_count, bootinfo);
    ca_monitor_init_protocon_states(bootinfo->num_pc);
    monitor_acl_initialise(bootinfo->num_pc);
    TSLDR_DBG_PRINT(PROGNAME "Boot phase third: %lu protocons ready\n", bootinfo->num_pc);
}

static void ca_monitor_boot(void)
{
    /* Filesystem calls suspend this coroutine until their channel completes.
     * Each phase then returns normally before the next phase begins. */
    ca_monitor_boot_phase_first();
    ca_monitor_boot_phase_second();
    ca_monitor_boot_phase_third();
}

void ca_monitor_init_system(void *binfo)
{
    ca_monitor_init_cothread_spawn(ca_monitor_boot,
                                   binfo,
                                   "failed to spawn boot coroutine.\n");
    microkit_cothread_yield();
}
