/*
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdint.h>

#include "control.h"
#include "coremark.h"

#define COREMARK_STOP_TIME_NS 1000000000ULL

static bool measured_run;

void coremark_port_yield(void)
{
}

ee_u8 core_start_parallel(core_results *results)
{
    iterate(results);
    return 0;
}

ee_u8 core_stop_parallel(core_results *results)
{
    (void)results;
    return 0;
}

void trace_coremark_putchar(char character)
{
    (void)character;
}

uint64_t trace_coremark_now_ns(void)
{
    sched_bench_control_t *control = sched_bench_control();

    if (!measured_run) {
        return 0;
    }

    if (sched_bench_victim_is_active(control)) {
        /* core_portme.c samples time once at every complete iteration. Use
         * that boundary to update throughput without modifying CoreMark. */
        sched_bench_iteration_completed(control);
        return 0;
    }

    /* The original port stops after BENCH_SECONDS. Returning its one-second
     * deadline translates the issuer's stop flag into that existing policy. */
    return COREMARK_STOP_TIME_NS;
}

void trace_coremark_io_start(uint64_t now)
{
    (void)now;
    measured_run = true;
}

void trace_coremark_io_stop(uint64_t now)
{
    (void)now;
    measured_run = false;
}

