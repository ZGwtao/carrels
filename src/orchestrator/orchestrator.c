/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <microkit.h>
#include <sddf/timer/config.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/util/printf.h>

#include <libtrustedlo.h>
#include <libmicrokitco.h>
#include <lions/fs/config.h>
#include <pico_vfs.h>
#include <misc.h>

#define PROGNAME "[@orchestrator] "

#define FNAME_BUF_SIZE 64
#define MIN_REQ_PC_NUM 1U
#define MAX_REQ_PC_NUM 4U

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

uintptr_t shared1 = 0x4000000;
uintptr_t shared2 = 0xb000000;
uintptr_t shared3 = 0x6000000;

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;

static char mp_stack1[0x10000];
static char mp_stack2[0x10000];
static co_control_t co_controller_mem;

static void blocking_wait(microkit_channel ch) { microkit_cothread_wait_on_channel(ch); }

serial_queue_handle_t serial_rx_queue_handle;
serial_queue_handle_t serial_tx_queue_handle;

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

bool fs_init;

#define MIN_REQ_PC_NUM 1U
#define MAX_REQ_PC_NUM 4U

uint32_t req_pc_num = MIN_REQ_PC_NUM;

static microrl_t shell;
static char fname_buf[FNAME_BUF_SIZE];

static const char *const shell_commands[] = {
    "start",
    "lspcs",
    "flip",
    "stop",
    "hang",
    "resume",
    "help",
};

#if MICRORL_CFG_USE_COMPLETE
static char *completion_words[ARRAY_SIZE(shell_commands) + 1];
#endif

static int shell_output(microrl_t *mrl, const char *str)
{
    int count = 0;

    MICRORL_UNUSED(mrl);

    if (str == NULL) {
        return 0;
    }

    while (*str != '\0') {
        sddf_putchar_unbuffered(*str++);
        count++;
    }

    return count;
}

static bool parse_u32_decimal(const char *text, uint32_t *value_out)
{
    uint32_t value = 0;

    if (text == NULL || value_out == NULL || *text == '\0') {
        return false;
    }

    while (*text != '\0') {
        uint32_t digit;

        if (*text < '0' || *text > '9') {
            return false;
        }

        digit = (uint32_t)(*text - '0');

        if (value > (UINT32_MAX - digit) / 10U) {
            return false;
        }

        value = value * 10U + digit;
        text++;
    }

    *value_out = value;
    return true;
}

static inline
void shell_inst_epilogue(void)
{
    sddf_printf("\r\nType: \"Ctrl \\\\ 0\" to return\r\n");
}

static void shell_print_help(void)
{
    sddf_printf(
        "Commands:\r\n"
        "  start <elf> [pc_num]  Load and start an ELF; pc_num is %u..%u\r\n"
        "  lspcs                 List proto-containers\r\n"
        "  flip                  Flip the ACL rule\r\n"
        "  stop -i <pd_id>       Stop a protection domain\r\n"
        "  hang -i <pd_id>       Hang a protection domain\r\n"
        "  resume -i <pd_id>     Resume a protection domain\r\n"
        "  help                  Show this help\r\n",
        MIN_REQ_PC_NUM,
        MAX_REQ_PC_NUM
    );
}

void orchestrator_prologue(void)
{
    TSLDR_DBG_PRINT(PROGNAME "(fs mount) start fs initialisation\n");
    fs_cmpl_t completion;
    int err = fs_command_blocking(&completion, (fs_cmd_t){ .type = FS_CMD_INITIALISE });
    if (err || completion.status != FS_STATUS_SUCCESS) {
        TSLDR_DBG_PRINT(PROGNAME "MP|ERROR: Failed to mount\n");
    }
    fs_init = true;

    TSLDR_DBG_PRINT(PROGNAME "(fs mount) finished fs initialisation\n");

    pico_vfs_readfile2buf((void *)shared1, "protocon.elf", &err);
    if (err != seL4_NoError) {
        return;
    }
    TSLDR_DBG_PRINT(PROGNAME "Wrote proto-container's ELF file into memory\n");

    pico_vfs_readfile2buf((void *)shared3, "trampoline.elf", &err);
    if (err != seL4_NoError) {
        return;
    }
    TSLDR_DBG_PRINT(PROGNAME "Wrote trampoline's ELF file into memory\n");

    shell_output(&shell, "orche@>$ \n");
    shell_print_help();
    shell_output(&shell, "orche@>$ ");
}

void load_elf_payload(void)
{
    while(!fs_init) {
        microkit_cothread_yield();
    }
    TSLDR_DBG_PRINT(PROGNAME "entry of load_elf_payload\n");

    microkit_msginfo info;
    seL4_Error error;

    pico_vfs_readfile2buf((void *)shared2, fname_buf, &error);
    if (error != seL4_NoError) {
        TSLDR_DBG_PRINT(PROGNAME "Failed to read %s\n", fname_buf);
        shell_inst_epilogue();
        return;
    }
    TSLDR_DBG_PRINT(PROGNAME "Wrote test's ELF file into memory\n");

    microkit_mr_set(0, 1);
    microkit_mr_set(1, req_pc_num);
    info = microkit_ppcall(1, microkit_msginfo_new(0, 2));
    error = microkit_msginfo_get_label(info);
    if (error != seL4_NoError) {
        microkit_internal_crash(error);
    }
}

static int cmd_start(int argc, const char *const *argv)
{
    uint32_t requested_pc_num = MIN_REQ_PC_NUM;
    size_t filename_len;

    if (argc != 2 && argc != 3) {
        sddf_printf("Usage: start <elf> [pc_num]\r\n");
        return 1;
    }

    filename_len = strlen(argv[1]);
    if (filename_len == 0 || filename_len >= sizeof(fname_buf)) {
        sddf_printf(
            "ELF filename must contain 1..%u characters\r\n",
            (unsigned int)(sizeof(fname_buf) - 1)
        );
        return 1;
    }

    if (argc == 3) {
        if (!parse_u32_decimal(argv[2], &requested_pc_num) ||
            requested_pc_num < MIN_REQ_PC_NUM ||
            requested_pc_num > MAX_REQ_PC_NUM) {
            sddf_printf(
                "pc_num must be an integer from %u to %u\r\n",
                MIN_REQ_PC_NUM,
                MAX_REQ_PC_NUM
            );
            return 1;
        }
    }

    memcpy(fname_buf, argv[1], filename_len + 1);
    req_pc_num = requested_pc_num;

    if (microkit_cothread_spawn(load_elf_payload, NULL) ==
        LIBMICROKITCO_NULL_HANDLE) {
        TSLDR_DBG_PRINT(PROGNAME "Cannot spawn cothread to load payload\n");
        sddf_printf("Failed to start payload loader\r\n");
        return 1;
    }

    microkit_cothread_yield();
    return 0;
}

static int call_monitor(seL4_Word syscall_id,
                        bool has_argument,
                        seL4_Word argument)
{
    microkit_msginfo info;
    seL4_Error error;

    microkit_mr_set(0, syscall_id);

    if (has_argument) {
        microkit_mr_set(1, argument);
        info = microkit_ppcall(1, microkit_msginfo_new(0, 2));
    } else {
        info = microkit_ppcall(1, microkit_msginfo_new(0, 1));
    }

    error = microkit_msginfo_get_label(info);
    if (error != seL4_NoError) {
        microkit_internal_crash(error);
    }

    return 0;
}

static int cmd_no_argument(int argc,
                           seL4_Word syscall_id,
                           const char *usage)
{
    if (argc != 1) {
        sddf_printf("Usage: %s\r\n", usage);
        return 1;
    }

    return call_monitor(syscall_id, false, 0);
}

static int cmd_pd_control(int argc,
                          const char *const *argv,
                          seL4_Word syscall_id,
                          const char *command_name)
{
    uint32_t pd_id;

    if (argc != 3 || strcmp(argv[1], "-i") != 0) {
        sddf_printf("Usage: %s -i <pd_id>\r\n", command_name);
        return 1;
    }

    if (!parse_u32_decimal(argv[2], &pd_id)) {
        sddf_printf("Invalid PD id: %s\r\n", argv[2]);
        return 1;
    }

    return call_monitor(syscall_id, true, (seL4_Word)pd_id);
}

static int shell_execute(microrl_t *mrl,
                         int argc,
                         const char *const *argv)
{
    MICRORL_UNUSED(mrl);

    if (argc == 0) {
        return 0;
    }

    if (strcmp(argv[0], "start") == 0) {
        return cmd_start(argc, argv);
    }

    if (strcmp(argv[0], "lspcs") == 0) {
        return cmd_no_argument(argc, 5, "lspcs");
    }

    if (strcmp(argv[0], "flip") == 0) {
        return cmd_no_argument(argc, 17, "flip");
    }

    if (strcmp(argv[0], "stop") == 0) {
        return cmd_pd_control(argc, argv, 6, "stop");
    }

    if (strcmp(argv[0], "hang") == 0) {
        return cmd_pd_control(argc, argv, 3, "hang");
    }

    if (strcmp(argv[0], "resume") == 0) {
        return cmd_pd_control(argc, argv, 4, "resume");
    }

    if (strcmp(argv[0], "help") == 0) {
        if (argc != 1) {
            sddf_printf("Usage: help\r\n");
            return 1;
        }

        shell_print_help();
        return 0;
    }

    sddf_printf("Unknown command: %s\r\n", argv[0]);
    return 1;
}

#if MICRORL_CFG_USE_COMPLETE
static char **shell_complete(microrl_t *mrl,
                             int argc,
                             const char *const *argv)
{
    size_t matches = 0;
    size_t i;
    const char *prefix = "";

    MICRORL_UNUSED(mrl);

    if (argc > 1) {
        completion_words[0] = NULL;
        return completion_words;
    }

    if (argc == 1) {
        prefix = argv[0];
    }

    for (i = 0; i < ARRAY_SIZE(shell_commands); i++) {
        size_t prefix_len = strlen(prefix);

        if (strncmp(shell_commands[i], prefix, prefix_len) == 0) {
            completion_words[matches++] = (char *)shell_commands[i];
        }
    }

    completion_words[matches] = NULL;
    return completion_words;
}
#endif

#if MICRORL_CFG_USE_CTRL_C
static void shell_sigint(microrl_t *mrl)
{
    shell_output(mrl, "^C\r\n");
}
#endif

void init(void)
{
    microrlr_t result;

    TSLDR_DBG_PRINT(PROGNAME "Entered init\n");
    assert(serial_config_check_magic(&serial_config));
    TSLDR_DBG_PRINT(PROGNAME "check serial config\n");
    assert(fs_config_check_magic(&fs_config));
    TSLDR_DBG_PRINT(PROGNAME "check fs config\n");

    if (serial_config.rx.queue.vaddr != NULL) {
        serial_queue_init(&serial_rx_queue_handle, serial_config.rx.queue.vaddr, serial_config.rx.data.size, serial_config.rx.data.vaddr);
    }
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size, serial_config.tx.data.vaddr);
    serial_putchar_init(serial_config.tx.id, &serial_tx_queue_handle);

    
    fs_set_blocking_wait(blocking_wait);
    fs_command_queue = fs_config.server.command_queue.vaddr;
    fs_completion_queue = fs_config.server.completion_queue.vaddr;
    fs_share = fs_config.server.share.vaddr;
    fs_init = false;

    TSLDR_DBG_PRINT(PROGNAME "finalised init\n");

    stack_ptrs_arg_array_t costacks = { (uintptr_t) mp_stack1, (uintptr_t) mp_stack2 };
    microkit_cothread_init(&co_controller_mem, 0x10000, costacks);

    if (microkit_cothread_spawn(orchestrator_prologue, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        TSLDR_DBG_PRINT(PROGNAME "Cannot initialise orchestrator cothread2\n");
        microkit_internal_crash(-1);
    }
    microkit_cothread_yield();

    result = microrl_init(&shell, shell_output, shell_execute);
    if (result != microrlOK) {
        TSLDR_DBG_PRINT(PROGNAME "microrl_init failed: %d\n", result);
        microkit_internal_crash(-1);
    }

#if MICRORL_CFG_USE_COMPLETE
    result = microrl_set_complete_callback(&shell, shell_complete);
    if (result != microrlOK) {
        TSLDR_DBG_PRINT(
            PROGNAME "microrl_set_complete_callback failed: %d\n",
            result
        );
        microkit_internal_crash(-1);
    }
#endif

#if MICRORL_CFG_USE_CTRL_C
    result = microrl_set_sigint_callback(&shell, shell_sigint);
    if (result != microrlOK) {
        TSLDR_DBG_PRINT(
            PROGNAME "microrl_set_sigint_callback failed: %d\n",
            result
        );
        microkit_internal_crash(-1);
    }
#endif

    TSLDR_DBG_PRINT(PROGNAME "finished init\n");
}

void notified(microkit_channel ch)
{
    fs_process_completions(NULL);
    microkit_cothread_recv_ntfn(ch);

    if (ch == serial_config.rx.id) {
        char c;
        while (!serial_dequeue(&serial_rx_queue_handle, &c)) {
            microrlr_t result = microrl_processing_input(&shell, &c, 1);

            if (result != microrlOK && result != microrlERRCLFULL) {
                TSLDR_DBG_PRINT(
                    PROGNAME "microrl input error: %d\n",
                    result
                );
            }
        }
    } else if (ch == 30) {
        /* Notification from monitor. */
        shell_inst_epilogue();
    }
}
