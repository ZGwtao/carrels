/*
 * Copyright 2026, UNSW
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

#define MONITOR_CHANNEL 1
#define MONITOR_NOTIFICATION 30
#define PAYLOAD_VADDR 0x0b000000
#define COTHREAD_STACK_SIZE 0x10000
#define PROTOCON_COUNT 16
#ifndef COLD_START_COUNT
#define COLD_START_COUNT 100
#endif
#ifndef SAMPLE_COUNT
#define SAMPLE_COUNT 100
#endif

__attribute__((section(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((section(".timer_client_config"))) timer_client_config_t timer_config;
__attribute__((section(".fs_client_config"))) fs_client_config_t fs_config;

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

static serial_queue_handle_t serial_tx_queue;
static co_control_t cothread_control;
static char cothread_stacks[2][COTHREAD_STACK_SIZE];

typedef struct {
    const char *name;
    const char *image;
} benchmark_case_t;

typedef struct {
    unsigned int pc_id;
    uint64_t generation;
    uint64_t deploy_ns;
    uint64_t reclaim_ns;
    uint64_t profile_cycles[PC_PROFILE_COUNT];
} benchmark_sample_t;

typedef struct {
    benchmark_sample_t cold_start[COLD_START_COUNT];
    benchmark_sample_t steady_state[SAMPLE_COUNT];
} benchmark_result_t;

static const benchmark_case_t benchmark_cases[] = {
    {"native", "nop-native.img"},
    {"unikraft", "nop-unikraft.img"},
};

static benchmark_result_t benchmark_results[
    sizeof(benchmark_cases) / sizeof(benchmark_cases[0])
];

static const char *profile_names[PC_PROFILE_COUNT] = {
    "monitor_parse_plan_cycles",
    "monitor_payload_copy_cycles",
    "monitor_service_install_cycles",
    "monitor_start_cycles",
    "monitor_acl_release_cycles",
    "monitor_pd_stop_cycles",
    "monitor_loader_reset_cycles",
    "monitor_payload_clear_cycles",
    "monitor_running_to_terminate_cycles",
    "monitor_deploy_total_cycles",
    "monitor_reclaim_total_cycles",
    "monitor_lifecycle_total_cycles",
};

static uint64_t time_ns(void)
{
    return sddf_timer_time_now(timer_config.driver_id);
}

static void wait_for_monitor(void)
{
    microkit_cothread_wait_on_channel(MONITOR_NOTIFICATION);
}

static void initialise_ramfs(void)
{
    fs_cmpl_t completion;
    int error = fs_command_blocking(
        &completion,
        (fs_cmd_t){.type = FS_CMD_INITIALISE}
    );

    if (error || completion.status != FS_STATUS_SUCCESS) {
        sddf_printf("BENCH|ERROR|ramfs-initialise\n");
        microkit_internal_crash(error ? error : -1);
    }
}

static void deploy(unsigned int pc_id)
{
    microkit_mr_set(0, PC_MONITOR_CALL_DEPLOY_TO_PROTOCON);
    microkit_mr_set(1, pc_id);
    microkit_mr_set(2, 0);
    microkit_mr_set(3, 0);

    microkit_msginfo info = microkit_ppcall(
        MONITOR_CHANNEL,
        microkit_msginfo_new(0, 4)
    );
    if (microkit_msginfo_get_label(info) != MON_NO_ERROR) {
        sddf_printf("BENCH|ERROR|deploy-request|pc=%u|error=%lu\n",
                    pc_id,
                    microkit_msginfo_get_label(info));
        microkit_internal_crash(-1);
    }
}

static void wait_for_deployment(unsigned int pc_id)
{
    for (;;) {
        wait_for_monitor();

        microkit_mr_set(0, PC_MONITOR_CALL_DEPLOY_RESULT);
        microkit_msginfo info = microkit_ppcall(
            MONITOR_CHANNEL,
            microkit_msginfo_new(0, 1)
        );
        seL4_Word error = microkit_msginfo_get_label(info);

        if (error == MON_DEPLOY_IN_PROGRESS) {
            continue;
        }

        pc_deploy_result_t result = {
            .requested_count =
                (uint32_t)microkit_mr_get(PC_DEPLOY_RESULT_REQUESTED_COUNT),
            .deployed_count =
                (uint32_t)microkit_mr_get(PC_DEPLOY_RESULT_DEPLOYED_COUNT),
            .protocon_mask =
                (uint32_t)microkit_mr_get(PC_DEPLOY_RESULT_PROTOCON_MASK),
        };

        if (error != MON_NO_ERROR || result.requested_count != 1 ||
            result.deployed_count != 1 || result.protocon_mask != ((uint32_t)1U << pc_id)) {
            sddf_printf("BENCH|ERROR|deploy-result|pc=%u|error=%lu\n",
                        pc_id,
                        error);
            microkit_internal_crash(-1);
        }
        return;
    }
}

static bool protocon_is_running(unsigned int pc_id)
{
    microkit_mr_set(0, PC_MONITOR_CALL_QUERY_PROTOCONS);
    microkit_msginfo info = microkit_ppcall(
        MONITOR_CHANNEL,
        microkit_msginfo_new(0, 1)
    );
    if (microkit_msginfo_get_label(info) != MON_NO_ERROR) {
        sddf_printf("BENCH|ERROR|query|pc=%u\n", pc_id);
        microkit_internal_crash(-1);
    }

    return (microkit_mr_get(0) & ((seL4_Word)1U << pc_id)) != 0;
}

static void wait_for_reclamation(unsigned int pc_id)
{
    while (protocon_is_running(pc_id)) {
        wait_for_monitor();
    }
}

static uint64_t profile_cycles(unsigned int pc_id,
                               unsigned int metric,
                               uint64_t *generation,
                               pc_profile_state_t *state)
{
    microkit_mr_set(0, PC_MONITOR_CALL_PROFILE);
    microkit_mr_set(1, pc_id);
    microkit_mr_set(2, metric);
    microkit_msginfo info = microkit_ppcall(MONITOR_CHANNEL, microkit_msginfo_new(0, 3));
    assert(microkit_msginfo_get_label(info) == MON_NO_ERROR);

    *generation = microkit_mr_get(1);
    *state = (pc_profile_state_t)microkit_mr_get(2);
    return microkit_mr_get(0);
}

static void run_iteration(unsigned int pc_id, benchmark_sample_t *sample)
{
#if defined(BENCH_PROGRESS)
    sddf_printf("BENCH|progress=iteration-start|pc=%u\n", pc_id);
#endif
    uint64_t start = time_ns();

    deploy(pc_id);
#if defined(BENCH_PROGRESS)
    sddf_printf("BENCH|progress=deploy-requested|pc=%u\n", pc_id);
#endif
    wait_for_deployment(pc_id);

    uint64_t deployed = time_ns();
#if defined(BENCH_PROGRESS)
    sddf_printf("BENCH|progress=deploy-complete|pc=%u\n", pc_id);
#endif

    wait_for_reclamation(pc_id);
#if defined(BENCH_PROGRESS)
    sddf_printf("BENCH|progress=reclaim-complete|pc=%u\n", pc_id);
#endif

    sample->pc_id = pc_id;
    sample->deploy_ns = deployed - start;
    sample->reclaim_ns = time_ns() - deployed;

    pc_profile_state_t state;
    for (unsigned int metric = 0; metric < PC_PROFILE_COUNT; ++metric) {
        uint64_t generation;
        sample->profile_cycles[metric] = profile_cycles(pc_id, metric, &generation, &state);
        if (metric == 0) {
            sample->generation = generation;
        } else if (sample->generation != generation) {
            sddf_printf("BENCH|ERROR|profile-generation|pc=%u|expected=%lu|actual=%lu\n",
                        pc_id,
                        sample->generation,
                        generation);
            microkit_internal_crash(-1);
        }
    }
    if (state != PC_PROFILE_STATE_COMPLETE) {
        sddf_printf("BENCH|ERROR|profile-state|pc=%u|generation=%lu|state=%u\n",
                    pc_id,
                    sample->generation,
                    (unsigned int)state);
        microkit_internal_crash(-1);
    }
#if defined(BENCH_PROGRESS)
    sddf_printf("BENCH|progress=iteration-complete|pc=%u|generation=%lu\n",
                pc_id,
                sample->generation);
#endif
}

static void sort_samples(uint64_t *samples, unsigned int count)
{
    for (unsigned int i = 1; i < count; i++) {
        uint64_t value = samples[i];
        unsigned int j = i;

        while (j > 0 && samples[j - 1] > value) {
            samples[j] = samples[j - 1];
            j--;
        }
        samples[j] = value;
    }
}

static void report_samples(const char *app, const char *metric,
                           uint64_t *samples, unsigned int count)
{
    sort_samples(samples, count);
    uint64_t median = count % 2 == 0
        ? (samples[count / 2 - 1] + samples[count / 2]) / 2
        : samples[count / 2];
    unsigned int p95 = (95 * count + 99) / 100 - 1;
    unsigned int p99 = (99 * count + 99) / 100 - 1;

    sddf_printf("BENCH|app=%s|series=steady-state|metric=%s|samples=%u|"
                "min=%lu|median=%lu|"
                "p95=%lu|p99=%lu\n",
                app,
                metric,
                count,
                samples[0],
                median,
                samples[p95],
                samples[p99]);
}

static void load_image(const benchmark_case_t *benchmark)
{
    int error;
    pico_vfs_readfile2buf((void *)PAYLOAD_VADDR, benchmark->image, &error);

    if (error != seL4_NoError) {
        sddf_printf("BENCH|ERROR|read|app=%s\n", benchmark->name);
        microkit_internal_crash(error);
    }
}

static void run_case(const benchmark_case_t *benchmark, benchmark_result_t *result)
{
    load_image(benchmark);

    for (unsigned int i = 0; i < COLD_START_COUNT; i++) {
        run_iteration(i % PROTOCON_COUNT, &result->cold_start[i]);
    }

    for (unsigned int i = 0; i < SAMPLE_COUNT; i++) {
        run_iteration(i % PROTOCON_COUNT, &result->steady_state[i]);
    }
}

static void report_series(const benchmark_case_t *benchmark,
                          const char *series,
                          const benchmark_sample_t *samples,
                          unsigned int count)
{
    for (unsigned int i = 0; i < count; i++) {
        sddf_printf("BENCH|app=%s|series=%s|iteration=%u|pc=%u|generation=%lu|deploy_ns=%lu|"
                    "reclaim_ns=%lu|service_install_cycles=%lu\n",
                    benchmark->name,
                    series,
                    i,
                    samples[i].pc_id,
                    samples[i].generation,
                    samples[i].deploy_ns,
                    samples[i].reclaim_ns,
                    samples[i].profile_cycles[PC_PROFILE_DEPLOY_SERVICE_INSTALL]);
    }
}

static void report_case(const benchmark_case_t *benchmark,
                        const benchmark_result_t *result)
{
    uint64_t deploy_samples[SAMPLE_COUNT];
    uint64_t reclaim_samples[SAMPLE_COUNT];
    uint64_t profile_samples[PC_PROFILE_COUNT][SAMPLE_COUNT];

    report_series(benchmark, "cold-start", result->cold_start, COLD_START_COUNT);
    report_series(benchmark, "steady-state", result->steady_state, SAMPLE_COUNT);

    for (unsigned int i = 0; i < SAMPLE_COUNT; i++) {
        deploy_samples[i] = result->steady_state[i].deploy_ns;
        reclaim_samples[i] = result->steady_state[i].reclaim_ns;
        for (unsigned int metric = 0; metric < PC_PROFILE_COUNT; ++metric) {
            profile_samples[metric][i] = result->steady_state[i].profile_cycles[metric];
        }
    }

    report_samples(benchmark->name, "deploy_ns", deploy_samples, SAMPLE_COUNT);
    report_samples(benchmark->name, "reclaim_ns", reclaim_samples, SAMPLE_COUNT);
    for (unsigned int metric = 0; metric < PC_PROFILE_COUNT; ++metric) {
        report_samples(benchmark->name,
                       profile_names[metric],
                       profile_samples[metric],
                       SAMPLE_COUNT);
    }
}

static void benchmark_run(void)
{
    initialise_ramfs();

    for (unsigned int i = 0; i < sizeof(benchmark_cases) / sizeof(benchmark_cases[0]); i++) {
        run_case(&benchmark_cases[i], &benchmark_results[i]);
    }
    for (unsigned int i = 0; i < sizeof(benchmark_cases) / sizeof(benchmark_cases[0]); i++) {
        report_case(&benchmark_cases[i], &benchmark_results[i]);
    }

    sddf_printf("BENCH|DONE\n");
    for (;;) {
        microkit_cothread_yield();
    }
}

static void blocking_wait(microkit_channel channel)
{
    microkit_cothread_wait_on_channel(channel);
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

    fs_set_blocking_wait(blocking_wait);
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
