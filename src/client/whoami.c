/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <os/sddf.h>

#include <sddf/serial/config.h>
#include <sddf/serial/queue.h>
#include <sddf/timer/client.h>
#include <sddf/timer/config.h>
#include <sddf/util/printf.h>

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;

static serial_queue_handle_t serial_tx_queue_handle;
static uint64_t started_at_ns;

static void print_identity(uint64_t now_ns)
{
    sddf_printf("WHOAMI| slot=%s started_at=%lu ns uptime=%lu ns\n", sddf_get_pd_name(),
                (unsigned long)started_at_ns, (unsigned long)(now_ns - started_at_ns));
}

void init(void)
{
    assert(serial_config_check_magic(&serial_config));
    assert(timer_config_check_magic(&timer_config));

    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size,
                      serial_config.tx.data.vaddr);
    serial_putchar_init(serial_config.tx.id, &serial_tx_queue_handle);

    started_at_ns = sddf_timer_time_now(timer_config.driver_id);
    print_identity(started_at_ns);
    sddf_timer_set_timeout(timer_config.driver_id, NS_IN_S);
}

void notified(sddf_channel ch)
{
    if (ch != timer_config.driver_id) {
        return;
    }

    print_identity(sddf_timer_time_now(timer_config.driver_id));
    sddf_timer_set_timeout(timer_config.driver_id, NS_IN_S);
}
