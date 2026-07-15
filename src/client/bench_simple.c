#include <microkit.h>

#include <sddf/timer/client.h>
#include <sddf/timer/config.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <lions/fs/config.h>
#include <lions/fs/helpers.h>

#include <sddf/util/printf.h>
#include <pc_config.h>
#include <protocon.h>
#include <libtrustedlo.h>

#define MONITOR_PPC_CHANNEL (15)


__attribute__((__section__(".serial_client_config")))
serial_client_config_t serial_config;
__attribute__((__section__(".timer_client_config")))
timer_client_config_t timer_config;
__attribute__((__section__(".fs_client_config")))
fs_client_config_t fs_config;

serial_queue_handle_t serial_rx_queue_handle;
serial_queue_handle_t serial_tx_queue_handle;

__attribute__((__section__(".pc_svc_desc")))
const protocon_svc_desc_t ciface = {
    .t1_num = 1,
    .t2_num = 1,
    .t3_num = 1,
    .type1 = SERVICE_DEVICE_TIMER,
    .type2 = SERVICE_FILE_SYSTEM,
    .type3 = SERVICE_DEVICE_SERIAL,
    .t1_iface = { (uintptr_t)&timer_config, 0, 0, 0, 0, 0, 0, 0 },
    .t2_iface = { (uintptr_t)&fs_config, 0, 0, 0, 0, 0, 0, 0 },
    .t3_iface = { (uintptr_t)&serial_config, 0, 0, 0, 0, 0, 0, 0 },
};

sddf_channel timer_channel;

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

void init(void)
{
    assert(serial_config_check_magic(&serial_config));
    if (serial_config.rx.queue.vaddr != NULL) {
        serial_queue_init(&serial_rx_queue_handle,
                          serial_config.rx.queue.vaddr,
                          serial_config.rx.data.size,
                          serial_config.rx.data.vaddr);
    }
    serial_queue_init(&serial_tx_queue_handle,
                      serial_config.tx.queue.vaddr,
                      serial_config.tx.data.size,
                      serial_config.tx.data.vaddr);
    serial_putchar_init(serial_config.tx.id, &serial_tx_queue_handle);

    assert(timer_config_check_magic(&timer_config));

    timer_channel = timer_config.driver_id;

    fs_command_queue = fs_config.server.command_queue.vaddr;
    fs_completion_queue = fs_config.server.completion_queue.vaddr;
    fs_share = fs_config.server.share.vaddr;

    sddf_printf("bench_simple: point of exit\n");

    microkit_mr_set(0, 6);
    microkit_msginfo info =
        microkit_ppcall(MONITOR_PPC_CHANNEL,
                        microkit_msginfo_new(0, 1));
    seL4_Error error = microkit_msginfo_get_label(info);
    if (error != seL4_NoError) {
        microkit_internal_crash(error);
    }
}

void notified(microkit_channel ch) {}
