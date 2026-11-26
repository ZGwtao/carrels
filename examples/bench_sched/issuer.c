/*
 * SPDX-FileCopyrightText: 2026 UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include <microkit.h>
#include <libmicrokitco.h>

#include <lions/fs/config.h>
#include <lions/fs/helpers.h>
#include <lions/fs/protocol.h>
#include <sddf/serial/config.h>
#include <sddf/serial/queue.h>
#include <sddf/timer/client.h>
#include <sddf/timer/config.h>
#include <sddf/util/printf.h>

#include <carrels-user.h>
#include <ioutils/pico_vfs.h>
#include <pcmcall/error.h>

#include "control.h"

#define MONITOR_CHANNEL 1
#define MONITOR_NOTIFICATION 30
#define PAYLOAD_VADDR 0x0B000000
#define COTHREAD_STACK_SIZE 0x10000

#ifndef WARMUP_SECONDS
#define WARMUP_SECONDS 5
#endif

#ifndef MEASURE_SECONDS
#define MEASURE_SECONDS 30
#endif

#ifndef QUIET_SECONDS
#define QUIET_SECONDS 1
#endif

#ifndef TRIAL_COUNT
#define TRIAL_COUNT 5
#endif

#define NS_PER_SECOND 1000000000ULL

__attribute__((section(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((section(".timer_client_config"))) timer_client_config_t timer_config;
__attribute__((section(".fs_client_config"))) fs_client_config_t fs_config;

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

static serial_queue_handle_t serial_tx_queue;
static co_control_t cothread_control;
static char cothread_stacks[2][COTHREAD_STACK_SIZE];

typedef enum {
    LOAD_BASELINE,
    LOAD_TIMER,
    LOAD_LIFECYCLE,
    LOAD_SATURATION,
} load_kind_t;

typedef struct {
    const char *name;
    load_kind_t kind;
    uint32_t operations_per_second;
} load_config_t;

typedef struct {
    uint64_t iterations;
    uint64_t duration_ns;
    uint64_t operations;
    uint64_t missed_releases;
} trial_result_t;

static const load_config_t loads[] = {
    {.name = "baseline", .kind = LOAD_BASELINE},
    {.name = "timer-1", .kind = LOAD_TIMER, .operations_per_second = 1},
    {.name = "lifecycle-1", .kind = LOAD_LIFECYCLE, .operations_per_second = 1},
    {.name = "timer-10", .kind = LOAD_TIMER, .operations_per_second = 10},
    {.name = "lifecycle-10", .kind = LOAD_LIFECYCLE, .operations_per_second = 10},
    {.name = "timer-100", .kind = LOAD_TIMER, .operations_per_second = 100},
    {.name = "lifecycle-100", .kind = LOAD_LIFECYCLE, .operations_per_second = 100},
    {.name = "timer-500", .kind = LOAD_TIMER, .operations_per_second = 500},
    {.name = "lifecycle-500", .kind = LOAD_LIFECYCLE, .operations_per_second = 500},
    {.name = "saturation", .kind = LOAD_SATURATION},
};

#define LOAD_COUNT (sizeof(loads) / sizeof(loads[0]))

static trial_result_t results[LOAD_COUNT][TRIAL_COUNT];

static uint64_t time_now(void)
{
    return sddf_timer_time_now(timer_config.driver_id);
}

static void wait_on_channel(microkit_channel channel)
{
    microkit_cothread_wait_on_channel(channel);
}

static void wait_until(uint64_t deadline)
{
    uint64_t now = time_now();

    if (now >= deadline) {
        return;
    }

    sddf_timer_set_timeout(timer_config.driver_id, deadline - now);
    wait_on_channel(timer_config.driver_id);
}

static void initialise_ramfs(void)
{
    fs_cmpl_t completion;
    int error = fs_command_blocking(
        &completion,
        (fs_cmd_t){.type = FS_CMD_INITIALISE}
    );

    if (error || completion.status != FS_STATUS_SUCCESS) {
        sddf_printf("SCHED_BENCH|ERROR|ramfs-initialise\n");
        microkit_internal_crash(error ? error : -1);
    }
}

static void load_payload(void)
{
    int error;

    pico_vfs_readfile2buf((void *)PAYLOAD_VADDR, "nop-native.img", &error);
    if (error != seL4_NoError) {
        sddf_printf("SCHED_BENCH|ERROR|read-payload|error=%d\n", error);
        microkit_internal_crash(error);
    }
}

static seL4_Word request_deployment(void)
{
    microkit_mr_set(0, PC_MONITOR_CALL_DEPLOY);
    microkit_mr_set(1, 1);

    microkit_msginfo info = microkit_ppcall(
        MONITOR_CHANNEL,
        microkit_msginfo_new(0, 2)
    );

    return microkit_msginfo_get_label(info);
}

static uint32_t wait_for_deployment(void)
{
    for (;;) {
        wait_on_channel(MONITOR_NOTIFICATION);

        microkit_mr_set(0, PC_MONITOR_CALL_DEPLOY_RESULT);
        microkit_msginfo info = microkit_ppcall(
            MONITOR_CHANNEL,
            microkit_msginfo_new(0, 1)
        );
        seL4_Word error = microkit_msginfo_get_label(info);

        if (error == MON_DEPLOY_IN_PROGRESS) {
            continue;
        }

        uint32_t deployed_count =
            (uint32_t)microkit_mr_get(PC_DEPLOY_RESULT_DEPLOYED_COUNT);
        uint32_t protocon_mask =
            (uint32_t)microkit_mr_get(PC_DEPLOY_RESULT_PROTOCON_MASK);

        if (error != MON_NO_ERROR || deployed_count != 1 ||
            __builtin_popcount(protocon_mask) != 1) {
            sddf_printf("SCHED_BENCH|ERROR|deploy-result|count=%u|mask=0x%x|error=%lu\n",
                        deployed_count,
                        protocon_mask,
                        error);
            microkit_internal_crash(-1);
        }

        return protocon_mask;
    }
}

static uint32_t query_running_protocons(void)
{
    microkit_mr_set(0, PC_MONITOR_CALL_QUERY_PROTOCONS);
    microkit_msginfo info = microkit_ppcall(
        MONITOR_CHANNEL,
        microkit_msginfo_new(0, 1)
    );

    if (microkit_msginfo_get_label(info) != MON_NO_ERROR) {
        sddf_printf("SCHED_BENCH|ERROR|query-protocons\n");
        microkit_internal_crash(-1);
    }

    return (uint32_t)microkit_mr_get(0);
}

static void run_lifecycle_operation(void)
{
    seL4_Word error = request_deployment();

    if (error != MON_NO_ERROR) {
        sddf_printf("SCHED_BENCH|ERROR|deploy-request|error=%lu\n", error);
        microkit_internal_crash(-1);
    }

    uint32_t deployed_mask = wait_for_deployment();

    while (query_running_protocons() & deployed_mask) {
        wait_on_channel(MONITOR_NOTIFICATION);
    }
}

static void prepare_monitor(void)
{
    for (;;) {
        seL4_Word error = request_deployment();

        if (error == MON_INVALID_REQ_PC_NUM) {
            wait_until(time_now() + 1000000ULL);
            continue;
        }
        if (error != MON_NO_ERROR) {
            sddf_printf("SCHED_BENCH|ERROR|monitor-prepare|error=%lu\n", error);
            microkit_internal_crash(-1);
        }

        uint32_t deployed_mask = wait_for_deployment();
        while (query_running_protocons() & deployed_mask) {
            wait_on_channel(MONITOR_NOTIFICATION);
        }
        return;
    }
}

static void run_periodic_load(const load_config_t *load,
                              uint64_t deadline,
                              uint64_t *operations,
                              uint64_t *missed_releases)
{
    uint64_t period = NS_PER_SECOND / load->operations_per_second;
    uint64_t next_release = time_now();

    while (next_release < deadline) {
        wait_until(next_release);

        uint64_t observed_at = time_now();
        if (observed_at >= next_release + period) {
            uint64_t skipped = (observed_at - next_release) / period;

            *missed_releases += skipped;
            next_release += skipped * period;
        }

        if (load->kind == LOAD_LIFECYCLE) {
            run_lifecycle_operation();
        }

        (*operations)++;
        next_release += period;
    }

    wait_until(deadline);
}

static void run_load(const load_config_t *load,
                     uint64_t duration_ns,
                     uint64_t *operations,
                     uint64_t *missed_releases)
{
    uint64_t deadline = time_now() + duration_ns;

    if (load->kind == LOAD_BASELINE) {
        wait_until(deadline);
        return;
    }

    if (load->kind == LOAD_SATURATION) {
        while (time_now() < deadline) {
            run_lifecycle_operation();
            (*operations)++;
        }
        return;
    }

    run_periodic_load(load, deadline, operations, missed_releases);
}

static void start_victim(uint32_t generation)
{
    sched_bench_control_t *control = sched_bench_control();

    sched_bench_start_victim(control, generation);
    microkit_notify(SCHED_BENCH_VICTIM_CHANNEL);
}

static void stop_victim(uint32_t generation)
{
    sched_bench_control_t *control = sched_bench_control();

    sched_bench_stop_victim(control);
    while (!sched_bench_victim_has_completed(control, generation)) {
        wait_until(time_now() + 1000000ULL);
    }
}

static trial_result_t run_trial(const load_config_t *load, uint32_t generation)
{
    sched_bench_control_t *control = sched_bench_control();
    uint64_t ignored_operations = 0;
    uint64_t ignored_misses = 0;

    start_victim(generation);
    run_load(load,
             (uint64_t)WARMUP_SECONDS * NS_PER_SECOND,
             &ignored_operations,
             &ignored_misses);

    uint64_t iterations_before = sched_bench_iterations(control);
    uint64_t started_at = time_now();
    uint64_t operations = 0;
    uint64_t missed_releases = 0;

    run_load(load,
             (uint64_t)MEASURE_SECONDS * NS_PER_SECOND,
             &operations,
             &missed_releases);

    uint64_t finished_at = time_now();
    uint64_t iterations_after = sched_bench_iterations(control);

    stop_victim(generation);
    wait_until(time_now() + (uint64_t)QUIET_SECONDS * NS_PER_SECOND);

    return (trial_result_t){
        .iterations = iterations_after - iterations_before,
        .duration_ns = finished_at - started_at,
        .operations = operations,
        .missed_releases = missed_releases,
    };
}

static uint64_t throughput_milli(const trial_result_t *result)
{
    return result->iterations * 1000000000000ULL / result->duration_ns;
}

static uint64_t baseline_throughput(void)
{
    uint64_t sum = 0;

    for (unsigned int trial = 0; trial < TRIAL_COUNT; trial++) {
        sum += throughput_milli(&results[0][trial]);
    }

    return sum / TRIAL_COUNT;
}

static void report_results(void)
{
    uint64_t baseline = baseline_throughput();

    for (unsigned int load = 0; load < LOAD_COUNT; load++) {
        for (unsigned int trial = 0; trial < TRIAL_COUNT; trial++) {
            const trial_result_t *result = &results[load][trial];
            uint64_t throughput = throughput_milli(result);
            uint64_t loss_ppm = throughput < baseline
                ? (baseline - throughput) * 1000000ULL / baseline
                : 0;

            sddf_printf("SCHED_BENCH|load=%s|trial=%u|iterations=%lu|duration_ns=%lu|"
                        "throughput_milli_iter_s=%lu|loss_ppm=%lu|operations=%lu|"
                        "missed_releases=%lu\n",
                        loads[load].name,
                        trial,
                        result->iterations,
                        result->duration_ns,
                        throughput,
                        loss_ppm,
                        result->operations,
                        result->missed_releases);
        }
    }

    sddf_printf("SCHED_BENCH|DONE\n");
}

static void benchmark_run(void)
{
    sched_bench_control_t *control = sched_bench_control();
    uint32_t generation = 0;

    initialise_ramfs();
    load_payload();

    while (!sched_bench_victim_is_ready(control)) {
        wait_until(time_now() + 1000000ULL);
    }

    /* Wait for Monitor boot and warm the service cache before measuring CPU
     * interference. Neither one is part of the steady-state experiment. */
    prepare_monitor();

    sddf_printf("SCHED_BENCH|BEGIN|loads=%u|trials=%u|warmup_s=%u|measure_s=%u\n",
                (unsigned int)LOAD_COUNT,
                (unsigned int)TRIAL_COUNT,
                (unsigned int)WARMUP_SECONDS,
                (unsigned int)MEASURE_SECONDS);

    for (unsigned int load = 0; load < LOAD_COUNT; load++) {
        for (unsigned int trial = 0; trial < TRIAL_COUNT; trial++) {
#ifdef BENCH_PROGRESS
            sddf_printf("SCHED_BENCH|PROGRESS|load=%s|trial=%u|state=start\n",
                        loads[load].name,
                        trial);
#endif
            results[load][trial] = run_trial(&loads[load], ++generation);
#ifdef BENCH_PROGRESS
            sddf_printf("SCHED_BENCH|PROGRESS|load=%s|trial=%u|state=complete\n",
                        loads[load].name,
                        trial);
            wait_until(time_now() + NS_PER_SECOND);
#endif
        }
    }

    report_results();

    for (;;) {
        microkit_cothread_yield();
    }
}

void init(void)
{
    assert(serial_config_check_magic(&serial_config));
    assert(timer_config_check_magic(&timer_config));
    assert(fs_config_check_magic(&fs_config));

    serial_queue_init(&serial_tx_queue,
                      serial_config.tx.queue.vaddr,
                      serial_config.tx.data.size,
                      serial_config.tx.data.vaddr);
    serial_putchar_init(serial_config.tx.id, &serial_tx_queue);

    fs_set_blocking_wait(wait_on_channel);
    fs_command_queue = fs_config.server.command_queue.vaddr;
    fs_completion_queue = fs_config.server.completion_queue.vaddr;
    fs_share = fs_config.server.share.vaddr;

    stack_ptrs_arg_array_t stacks = {
        (uintptr_t)cothread_stacks[0],
        (uintptr_t)cothread_stacks[1],
    };
    microkit_cothread_init(&cothread_control, COTHREAD_STACK_SIZE, stacks);

    if (microkit_cothread_spawn(benchmark_run, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        microkit_internal_crash(-1);
    }

    microkit_cothread_yield();
}

void notified(microkit_channel channel)
{
    fs_process_completions(NULL);
    microkit_cothread_recv_ntfn(channel);
}

