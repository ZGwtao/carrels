/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <microkit.h>

#include <sddf/network/config.h>
#include <sddf/serial/config.h>
#include <sddf/timer/config.h>

#include <carrels-user.h>

__attribute__((section(".net_client_config"))) net_client_config_t net_config;
__attribute__((section(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((section(".timer_client_config"))) timer_client_config_t timer_config;

/* The dynamic loader patches these for every protocon payload. */
uintptr_t carrels_heap_base;
size_t carrels_heap_size;

void init(void)
{
    assert(net_config_check_magic(&net_config));
    assert(serial_config_check_magic(&serial_config));
    assert(timer_config_check_magic(&timer_config));

    microkit_mr_set(0, PC_MONITOR_CALL_TERMINATE);
    (void)microkit_ppcall(15, microkit_msginfo_new(0, 1));

    for (;;) {
        __asm__ volatile("" ::: "memory");
    }
}

void notified(microkit_channel ch)
{
    (void)ch;
}
