/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * Minimal native FATFS client.  This deliberately uses the asynchronous
 * protocol rather than fs_command_blocking(), because an ordinary native PD
 * does not have the monitor/orchestrator's cothread scheduler.
 */

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include <microkit.h>

#include <lions/fs/config.h>
#include <lions/fs/helpers.h>
#include <lions/fs/protocol.h>
#include <sddf/serial/config.h>
#include <sddf/serial/queue.h>
#include <sddf/timer/config.h>
#include <sddf/util/printf.h>

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;
__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;

/* Required by lib/fs/helpers/helpers.c. */
fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

static serial_queue_handle_t serial_tx_queue_handle;
static ptrdiff_t fs_buffer;
static uint64_t active_request;
static uint64_t file_descriptor;
static const char file_path[] = "native-client.txt";
static const char file_contents[] = "written by the native FATFS client\n";

enum fs_demo_state {
    FS_DEMO_MOUNT,
    FS_DEMO_OPEN_WRITE,
    FS_DEMO_WRITE,
    FS_DEMO_CLOSE_WRITE,
    FS_DEMO_OPEN_READ,
    FS_DEMO_READ,
    FS_DEMO_CLOSE_READ,
    FS_DEMO_DONE,
};

static enum fs_demo_state state;

static void issue(fs_cmd_t command)
{
    int error = fs_request_allocate(&active_request);
    assert(error == 0);
    command.id = active_request;
    fs_command_issue(command);
}

static void process_completion(uint64_t request_id)
{
    fs_cmpl_t completion;

    if (request_id != active_request) {
        return;
    }

    fs_command_complete(request_id, NULL, &completion);
    fs_request_free(request_id);

    if (completion.status != FS_STATUS_SUCCESS) {
        sddf_printf("FS-CLIENT|ERROR: command %u failed with status %u\n",
                    (unsigned)state,
                    (unsigned)completion.status);
        fs_buffer_free(fs_buffer);
        state = FS_DEMO_DONE;
        return;
    }

    switch (state) {
    case FS_DEMO_MOUNT:
        strcpy(fs_buffer_ptr(fs_buffer), file_path);
        state = FS_DEMO_OPEN_WRITE;
        issue((fs_cmd_t){
            .type = FS_CMD_FILE_OPEN,
            .params.file_open =
                {
                    .path = {.offset = fs_buffer, .size = strlen(fs_buffer_ptr(fs_buffer)) + 1},
                    .flags = FS_OPEN_FLAGS_READ_WRITE | FS_OPEN_FLAGS_CREATE,
                },
        });
        break;
    case FS_DEMO_OPEN_WRITE:
        file_descriptor = completion.data.file_open.fd;
        strcpy(fs_buffer_ptr(fs_buffer), file_contents);
        state = FS_DEMO_WRITE;
        issue((fs_cmd_t){
            .type = FS_CMD_FILE_WRITE,
            .params.file_write =
                {
                    .fd = file_descriptor,
                    .offset = 0,
                    .buf = {.offset = fs_buffer, .size = strlen(fs_buffer_ptr(fs_buffer))},
                },
        });
        break;
    case FS_DEMO_WRITE:
        assert(completion.data.file_write.len_written == sizeof(file_contents) - 1);
        state = FS_DEMO_CLOSE_WRITE;
        issue((fs_cmd_t){
            .type = FS_CMD_FILE_CLOSE,
            .params.file_close = {.fd = file_descriptor},
        });
        break;
    case FS_DEMO_CLOSE_WRITE:
        strcpy(fs_buffer_ptr(fs_buffer), file_path);
        state = FS_DEMO_OPEN_READ;
        issue((fs_cmd_t){
            .type = FS_CMD_FILE_OPEN,
            .params.file_open =
                {
                    .path = {.offset = fs_buffer, .size = sizeof(file_path)},
                    .flags = FS_OPEN_FLAGS_READ_ONLY,
                },
        });
        break;
    case FS_DEMO_OPEN_READ:
        file_descriptor = completion.data.file_open.fd;
        memset(fs_buffer_ptr(fs_buffer), 0, FS_BUFFER_SIZE);
        state = FS_DEMO_READ;
        issue((fs_cmd_t){
            .type = FS_CMD_FILE_READ,
            .params.file_read =
                {
                    .fd = file_descriptor,
                    .offset = 0,
                    .buf = {.offset = fs_buffer, .size = sizeof(file_contents) - 1},
                },
        });
        break;
    case FS_DEMO_READ:
        assert(completion.data.file_read.len_read == sizeof(file_contents) - 1);
        assert(memcmp(fs_buffer_ptr(fs_buffer), file_contents, sizeof(file_contents) - 1) == 0);
        state = FS_DEMO_CLOSE_READ;
        issue((fs_cmd_t){
            .type = FS_CMD_FILE_CLOSE,
            .params.file_close = {.fd = file_descriptor},
        });
        break;
    case FS_DEMO_CLOSE_READ:
        sddf_printf("FS-CLIENT|INFO: wrote, reopened, and verified native-client.txt\n");
        fs_buffer_free(fs_buffer);
        state = FS_DEMO_DONE;
        break;
    case FS_DEMO_DONE:
        break;
    }
}

void init(void)
{
    assert(serial_config_check_magic(&serial_config));
    assert(fs_config_check_magic(&fs_config));

    serial_queue_init(&serial_tx_queue_handle,
                      serial_config.tx.queue.vaddr,
                      serial_config.tx.data.size,
                      serial_config.tx.data.vaddr);
    serial_putchar_init(serial_config.tx.id, &serial_tx_queue_handle);

    fs_command_queue = fs_config.server.command_queue.vaddr;
    fs_completion_queue = fs_config.server.completion_queue.vaddr;
    fs_share = fs_config.server.share.vaddr;
    assert(fs_buffer_allocate(&fs_buffer) == 0);

    state = FS_DEMO_MOUNT;
    issue((fs_cmd_t){.type = FS_CMD_INITIALISE});
}

void notified(microkit_channel ch)
{
    if (ch == fs_config.server.id) {
        fs_process_completions(process_completion);
    }
}
