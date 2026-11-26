/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdint.h>

#include <microkit.h>

#include "control.h"

int coremark_main(void);

void init(void)
{
    sched_bench_victim_ready(sched_bench_control());
}

void notified(microkit_channel channel)
{
    sched_bench_control_t *control = sched_bench_control();

    if (channel != SCHED_BENCH_VICTIM_CHANNEL ||
        !sched_bench_victim_is_active(control)) {
        return;
    }

    uint32_t generation = sched_bench_victim_generation(control);

    coremark_main();
    sched_bench_victim_completed(control, generation);
}

