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

#ifndef WARMUP_COUNT
#define WARMUP_COUNT 100
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
    const char *image;
    uint64_t elf_size;
} payload_level_t;

typedef struct {
    uint64_t deploy_ns;
    uint64_t reclaim_ns;
    uint64_t lifecycle_ns;
} latency_sample_t;

typedef struct {
    uint64_t minimum;
    uint64_t mean;
    uint64_t median;
    uint64_t p95;
    uint64_t p99;
    uint64_t maximum;
    uint64_t variance;
    uint64_t standard_deviation;
} latency_statistics_t;

static const payload_level_t payload_levels[] = {
    {"nop-131072.img", 131072},
    {"nop-262144.img", 262144},
    {"nop-524288.img", 524288},
    {"nop-1048576.img", 1048576},
    {"nop-2097152.img", 2097152},
    {"nop-4194304.img", 4194304},
    {"nop-8384512.img", 8384512},
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
        sddf_printf("PAYLOAD_BENCH|ERROR|ramfs-initialise\n");
        microkit_internal_crash(error ? error : -1);
    }
}

static void load_payload(const payload_level_t *payload)
{
    int error;
    pico_vfs_readfile2buf((void *)PAYLOAD_VADDR, payload->image, &error);

    if (error != seL4_NoError) {
        sddf_printf("PAYLOAD_BENCH|ERROR|read|image=%s\n", payload->image);
        microkit_internal_crash(error);
    }
}

static void request_deployment(unsigned int pc_id)
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
        sddf_printf("PAYLOAD_BENCH|ERROR|deploy-request|pc=%u|error=%lu\n",
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
            sddf_printf("PAYLOAD_BENCH|ERROR|deploy-result|pc=%u|error=%lu\n",
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
        sddf_printf("PAYLOAD_BENCH|ERROR|query|pc=%u\n", pc_id);
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

static latency_sample_t run_iteration(unsigned int pc_id)
{
    uint64_t started = time_ns();
    request_deployment(pc_id);
    wait_for_deployment(pc_id);

    uint64_t deployed = time_ns();
    wait_for_reclamation(pc_id);
    uint64_t reclaimed = time_ns();

    return (latency_sample_t){
        .deploy_ns = deployed - started,
        .reclaim_ns = reclaimed - deployed,
        .lifecycle_ns = reclaimed - started,
    };
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

static uint64_t integer_square_root(uint64_t value)
{
    uint64_t result = 0;
    uint64_t bit = (uint64_t)1 << 62;

    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

static latency_statistics_t calculate_statistics(uint64_t *samples,
                                                 unsigned int count)
{
    uint64_t sum = 0;
    for (unsigned int i = 0; i < count; i++) {
        sum += samples[i];
    }
    uint64_t mean = sum / count;

    uint64_t squared_deviation_sum = 0;
    for (unsigned int i = 0; i < count; i++) {
        uint64_t deviation = samples[i] > mean
            ? samples[i] - mean
            : mean - samples[i];
        squared_deviation_sum += deviation * deviation;
    }

    sort_samples(samples, count);
    uint64_t variance = squared_deviation_sum / count;
    return (latency_statistics_t){
        .minimum = samples[0],
        .mean = mean,
        .median = count % 2 == 0
            ? (samples[count / 2 - 1] + samples[count / 2]) / 2
            : samples[count / 2],
        .p95 = samples[(95 * count + 99) / 100 - 1],
        .p99 = samples[(99 * count + 99) / 100 - 1],
        .maximum = samples[count - 1],
        .variance = variance,
        .standard_deviation = integer_square_root(variance),
    };
}

static void report_statistics(const payload_level_t *payload,
                              const char *metric,
                              uint64_t *samples)
{
    latency_statistics_t stats = calculate_statistics(samples, SAMPLE_COUNT);

    sddf_printf("PAYLOAD_BENCH|elf_size=%lu|metric=%s|samples=%u|"
                "min_ns=%lu|mean_ns=%lu|median_ns=%lu|p95_ns=%lu|p99_ns=%lu|"
                "max_ns=%lu|variance_ns2=%lu|stddev_ns=%lu\n",
                payload->elf_size,
                metric,
                SAMPLE_COUNT,
                stats.minimum,
                stats.mean,
                stats.median,
                stats.p95,
                stats.p99,
                stats.maximum,
                stats.variance,
                stats.standard_deviation);
}

static void benchmark_payload(const payload_level_t *payload)
{
    latency_sample_t samples[SAMPLE_COUNT];
    uint64_t deploy_samples[SAMPLE_COUNT];
    uint64_t reclaim_samples[SAMPLE_COUNT];
    uint64_t lifecycle_samples[SAMPLE_COUNT];

    load_payload(payload);

    for (unsigned int i = 0; i < WARMUP_COUNT; i++) {
        (void)run_iteration(i % PROTOCON_COUNT);
    }
    for (unsigned int i = 0; i < SAMPLE_COUNT; i++) {
        samples[i] = run_iteration(i % PROTOCON_COUNT);
    }

    for (unsigned int i = 0; i < SAMPLE_COUNT; i++) {
        deploy_samples[i] = samples[i].deploy_ns;
        reclaim_samples[i] = samples[i].reclaim_ns;
        lifecycle_samples[i] = samples[i].lifecycle_ns;
    }

    report_statistics(payload, "deploy", deploy_samples);
    report_statistics(payload, "reclaim", reclaim_samples);
    report_statistics(payload, "lifecycle", lifecycle_samples);
}

static void benchmark_run(void)
{
    initialise_ramfs();

    for (unsigned int i = 0; i < sizeof(payload_levels) / sizeof(payload_levels[0]); i++) {
        benchmark_payload(&payload_levels[i]);
    }

    sddf_printf("PAYLOAD_BENCH|DONE\n");
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
