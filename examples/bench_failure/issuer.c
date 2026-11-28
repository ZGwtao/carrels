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

#define MONITOR_CHANNEL 1
#define MONITOR_NOTIFICATION 30
#define PAYLOAD_VADDR 0x0B000000
#define COTHREAD_STACK_SIZE 0x10000
#define PROTOCON_COUNT 16

#ifndef WARMUP_COUNT
#define WARMUP_COUNT 10
#endif

#ifndef SAMPLE_COUNT
#define SAMPLE_COUNT 100
#endif

#ifndef VM_FAULT_STRESS_COUNT
#define VM_FAULT_STRESS_COUNT 0
#endif

#define VM_FAULT_STRESS_PROGRESS_INTERVAL 1000

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
    FAILURE_REJECTED,
    FAILURE_RECOVERED,
} failure_kind_t;

typedef struct {
    const char *name;
    const char *image;
    failure_kind_t kind;
    pc_exit_reason_t exit_reason;
} failure_case_t;

typedef struct {
    uint64_t response_ns;
    uint64_t total_ns;
} failure_sample_t;

typedef struct {
    failure_sample_t samples[SAMPLE_COUNT];
} failure_result_t;

static const failure_case_t failure_cases[] = {
    {"bad-manifest-magic", "bad-manifest-magic.img", FAILURE_REJECTED, PC_EXIT_NONE},
    {"oversized-payload", "oversized-payload.img", FAILURE_REJECTED, PC_EXIT_NONE},
    {"bad-elf-magic", "bad-elf-magic.img", FAILURE_REJECTED, PC_EXIT_NONE},
    {"bad-section-table", "bad-section-table.img", FAILURE_REJECTED, PC_EXIT_NONE},
    {"vm-fault", "vm-fault.img", FAILURE_RECOVERED, PC_EXIT_FAULTED},
    {"illegal-monitor-call", "illegal-call.img", FAILURE_RECOVERED, PC_EXIT_INVALID_CALL},
};

#define FAILURE_CASE_COUNT (sizeof(failure_cases) / sizeof(failure_cases[0]))

static failure_result_t results[FAILURE_CASE_COUNT];

static uint64_t time_now(void)
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
    int error = fs_command_blocking(&completion, (fs_cmd_t){.type = FS_CMD_INITIALISE});

    if (error || completion.status != FS_STATUS_SUCCESS) {
        sddf_printf("FAILURE_BENCH|ERROR|ramfs-initialise\n");
        microkit_internal_crash(error ? error : -1);
    }
}

static void load_image(const char *image)
{
    int error;

    (void)pico_vfs_readfile2buf((void *)PAYLOAD_VADDR, image, &error);
    if (error != seL4_NoError) {
        sddf_printf("FAILURE_BENCH|ERROR|read-image|image=%s|error=%d\n", image, error);
        microkit_internal_crash(error);
    }
}

static bool protocon_is_running(unsigned int pc_id)
{
    microkit_mr_set(0, PC_MONITOR_CALL_QUERY_PROTOCONS);
    microkit_msginfo info = microkit_ppcall(MONITOR_CHANNEL, microkit_msginfo_new(0, 1));

    if (microkit_msginfo_get_label(info) != MON_NO_ERROR) {
        sddf_printf("FAILURE_BENCH|ERROR|query|pc=%u\n", pc_id);
        microkit_internal_crash(-1);
    }

    return (microkit_mr_get(0) & ((seL4_Word)1U << pc_id)) != 0;
}

static pc_exit_reason_t query_exit_reason(unsigned int pc_id)
{
    microkit_mr_set(0, PC_MONITOR_CALL_QUERY_EXIT_REASON);
    microkit_mr_set(1, pc_id);
    microkit_msginfo info = microkit_ppcall(MONITOR_CHANNEL, microkit_msginfo_new(0, 2));

    if (microkit_msginfo_get_label(info) != MON_NO_ERROR) {
        sddf_printf("FAILURE_BENCH|ERROR|query-exit-reason|pc=%u\n", pc_id);
        microkit_internal_crash(-1);
    }

    return (pc_exit_reason_t)microkit_mr_get(0);
}

static void expect_exit_reason(unsigned int pc_id, pc_exit_reason_t expected)
{
    pc_exit_reason_t observed = query_exit_reason(pc_id);

    if (observed != expected) {
        sddf_printf("FAILURE_BENCH|ERROR|exit-reason|pc=%u|expected=%u|observed=%u\n",
                    pc_id,
                    (unsigned int)expected,
                    (unsigned int)observed);
        microkit_internal_crash(-1);
    }
}

static seL4_Word request_deployment(unsigned int pc_id)
{
    microkit_mr_set(0, PC_MONITOR_CALL_DEPLOY_TO_PROTOCON);
    microkit_mr_set(1, pc_id);
    microkit_mr_set(2, 0);
    microkit_mr_set(3, 0);

    microkit_msginfo info = microkit_ppcall(MONITOR_CHANNEL, microkit_msginfo_new(0, 4));

    return microkit_msginfo_get_label(info);
}

static seL4_Word wait_for_deployment(unsigned int pc_id, bool expect_success)
{
    for (;;) {
        microkit_mr_set(0, PC_MONITOR_CALL_DEPLOY_RESULT);
        microkit_msginfo info = microkit_ppcall(MONITOR_CHANNEL, microkit_msginfo_new(0, 1));
        seL4_Word error = microkit_msginfo_get_label(info);

        if (error == MON_DEPLOY_IN_PROGRESS) {
            wait_for_monitor();
            continue;
        }

        uint32_t deployed = (uint32_t)microkit_mr_get(PC_DEPLOY_RESULT_DEPLOYED_COUNT);
        uint32_t mask = (uint32_t)microkit_mr_get(PC_DEPLOY_RESULT_PROTOCON_MASK);

        if (expect_success) {
            if (error != MON_NO_ERROR || deployed != 1 || mask != ((uint32_t)1U << pc_id)) {
                sddf_printf("FAILURE_BENCH|ERROR|expected-deploy|pc=%u|error=%lu|"
                            "deployed=%u|mask=0x%x\n",
                            pc_id,
                            error,
                            deployed,
                            mask);
                microkit_internal_crash(-1);
            }
        } else if (error != MON_FAIL_TO_DEPLOY || deployed != 0 || mask != 0) {
            sddf_printf("FAILURE_BENCH|ERROR|expected-rejection|pc=%u|error=%lu|"
                        "deployed=%u|mask=0x%x\n",
                        pc_id,
                        error,
                        deployed,
                        mask);
            microkit_internal_crash(-1);
        }

        return error;
    }
}

static void wait_for_reclamation(unsigned int pc_id)
{
    /* Monitor notifies the issuer whenever a Protocon becomes reusable. The
     * state query handles coalesced notifications and confirms reclamation. */
    while (protocon_is_running(pc_id)) {
        wait_for_monitor();
    }
}

static void run_canary(unsigned int pc_id)
{
#ifdef BENCH_FAULT_TRACE
    sddf_printf("FAILURE_TRACE|event=canary_begin|child=%u\n", pc_id);
#endif
    load_image("canary.img");

    if (request_deployment(pc_id) != MON_NO_ERROR) {
        sddf_printf("FAILURE_BENCH|ERROR|canary-request|pc=%u\n", pc_id);
        microkit_internal_crash(-1);
    }

    (void)wait_for_deployment(pc_id, true);
#ifdef BENCH_FAULT_TRACE
    sddf_printf("FAILURE_TRACE|event=canary_deployed|child=%u\n", pc_id);
#endif
    wait_for_reclamation(pc_id);
    expect_exit_reason(pc_id, PC_EXIT_TERMINATED);
#ifdef BENCH_FAULT_TRACE
    sddf_printf("FAILURE_TRACE|event=canary_reclaimed|child=%u\n", pc_id);
#endif

    if (protocon_is_running(pc_id)) {
        sddf_printf("FAILURE_BENCH|ERROR|canary-not-reclaimed|pc=%u\n", pc_id);
        microkit_internal_crash(-1);
    }
}

static failure_sample_t run_rejection(const failure_case_t *test, unsigned int pc_id)
{
    load_image(test->image);
    uint64_t started_at = time_now();

    if (request_deployment(pc_id) != MON_NO_ERROR) {
        sddf_printf("FAILURE_BENCH|ERROR|rejection-request|case=%s|pc=%u\n", test->name, pc_id);
        microkit_internal_crash(-1);
    }

    (void)wait_for_deployment(pc_id, false);
    uint64_t rejected_at = time_now();

    if (protocon_is_running(pc_id)) {
        sddf_printf("FAILURE_BENCH|ERROR|rejected-pc-running|case=%s|pc=%u\n", test->name, pc_id);
        microkit_internal_crash(-1);
    }

    run_canary(pc_id);

    return (failure_sample_t){
        .response_ns = rejected_at - started_at,
        .total_ns = rejected_at - started_at,
    };
}

static failure_sample_t run_recovery(const failure_case_t *test, unsigned int pc_id)
{
    load_image(test->image);
    uint64_t started_at = time_now();

    if (request_deployment(pc_id) != MON_NO_ERROR) {
        sddf_printf("FAILURE_BENCH|ERROR|recovery-request|case=%s|pc=%u\n", test->name, pc_id);
        microkit_internal_crash(-1);
    }

    (void)wait_for_deployment(pc_id, true);
    uint64_t deployed_at = time_now();

    wait_for_reclamation(pc_id);
    uint64_t recovered_at = time_now();
    expect_exit_reason(pc_id, test->exit_reason);

    run_canary(pc_id);

    return (failure_sample_t){
        .response_ns = recovered_at - deployed_at,
        .total_ns = recovered_at - started_at,
    };
}

static void run_vm_fault_stress_iteration(unsigned int pc_id)
{
    load_image("vm-fault.img");

    if (request_deployment(pc_id) != MON_NO_ERROR) {
        sddf_printf("FAILURE_STRESS|ERROR|vm-fault-request|pc=%u\n", pc_id);
        microkit_internal_crash(-1);
    }

    (void)wait_for_deployment(pc_id, true);
    wait_for_reclamation(pc_id);
    expect_exit_reason(pc_id, PC_EXIT_FAULTED);
    run_canary(pc_id);
}

static void run_vm_fault_stress(void)
{
    sddf_printf("FAILURE_STRESS|BEGIN|iterations=%u\n", VM_FAULT_STRESS_COUNT);

    for (unsigned int i = 0; i < VM_FAULT_STRESS_COUNT; i++) {
        run_vm_fault_stress_iteration(i % PROTOCON_COUNT);

        if ((i + 1) % VM_FAULT_STRESS_PROGRESS_INTERVAL == 0) {
            sddf_printf("FAILURE_STRESS|PROGRESS|iterations=%u\n", i + 1);
        }
    }

    sddf_printf("FAILURE_STRESS|END|iterations=%u\n", VM_FAULT_STRESS_COUNT);
}

static failure_sample_t run_iteration(const failure_case_t *test, unsigned int pc_id)
{
    if (test->kind == FAILURE_REJECTED) {
        return run_rejection(test, pc_id);
    }

    return run_recovery(test, pc_id);
}

static void sort_samples(uint64_t samples[], unsigned int count)
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

static uint64_t square_root(uint64_t value)
{
    uint64_t low = 0;
    uint64_t high = UINT32_MAX;

    while (low < high) {
        uint64_t middle = low + (high - low) / 2 + 1;

        if (middle * middle <= value) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }

    return low;
}

static void report_statistics(const failure_case_t *test, const char *metric, uint64_t samples[])
{
    uint64_t sum = 0;

    for (unsigned int i = 0; i < SAMPLE_COUNT; i++) {
        sum += samples[i];
    }

    uint64_t mean = sum / SAMPLE_COUNT;
    uint64_t variance = 0;

    for (unsigned int i = 0; i < SAMPLE_COUNT; i++) {
        uint64_t difference = samples[i] >= mean ? samples[i] - mean : mean - samples[i];
        /* Failure responses are bounded well below one second in this
         * benchmark, so the squared nanosecond delta fits in uint64_t. Divide
         * each term before accumulation to avoid growing the sum by N. */
        variance += difference * difference / SAMPLE_COUNT;
    }

    sort_samples(samples, SAMPLE_COUNT);

    uint64_t median = SAMPLE_COUNT % 2 == 0
                          ? (samples[SAMPLE_COUNT / 2 - 1] + samples[SAMPLE_COUNT / 2]) / 2
                          : samples[SAMPLE_COUNT / 2];
    unsigned int p99 = (99 * SAMPLE_COUNT + 99) / 100 - 1;
    uint64_t standard_deviation = square_root(variance);

    sddf_printf("FAILURE_BENCH|case=%s|kind=%s|metric=%s|samples=%u|mean=%lu|"
                "median=%lu|p99=%lu|stddev=%lu\n",
                test->name,
                test->kind == FAILURE_REJECTED ? "rejection" : "recovery",
                metric,
                SAMPLE_COUNT,
                mean,
                median,
                samples[p99],
                standard_deviation);
}

static void report_case(const failure_case_t *test, const failure_result_t *result)
{
    uint64_t response[SAMPLE_COUNT];
    uint64_t total[SAMPLE_COUNT];

    for (unsigned int i = 0; i < SAMPLE_COUNT; i++) {
        response[i] = result->samples[i].response_ns;
        total[i] = result->samples[i].total_ns;

        sddf_printf("FAILURE_BENCH|case=%s|sample=%u|response_ns=%lu|total_ns=%lu\n",
                    test->name,
                    i,
                    response[i],
                    total[i]);
    }

    report_statistics(test, "response_ns", response);
    report_statistics(test, "total_ns", total);
}

static void run_case(const failure_case_t *test, failure_result_t *result)
{
    sddf_printf("FAILURE_BENCH|CASE_BEGIN|case=%s\n", test->name);

    for (unsigned int i = 0; i < WARMUP_COUNT; i++) {
        (void)run_iteration(test, i % PROTOCON_COUNT);
    }

    for (unsigned int i = 0; i < SAMPLE_COUNT; i++) {
#ifdef BENCH_PROGRESS
        sddf_printf("FAILURE_BENCH|PROGRESS|case=%s|sample=%u\n", test->name, i);
#endif
        result->samples[i] = run_iteration(test, i % PROTOCON_COUNT);

        if ((i + 1) % 10 == 0) {
            sddf_printf("FAILURE_BENCH|CASE_PROGRESS|case=%s|samples=%u\n", test->name, i + 1);
        }
    }

    sddf_printf("FAILURE_BENCH|CASE_END|case=%s\n", test->name);
}

static void benchmark_run(void)
{
    initialise_ramfs();

    /* Confirm that Monitor boot and the canary service-cache path are ready
     * before any failure latency is measured. */
    run_canary(0);

    if (VM_FAULT_STRESS_COUNT > 0) {
        run_vm_fault_stress();
    } else {
        for (unsigned int i = 0; i < FAILURE_CASE_COUNT; i++) {
            run_case(&failure_cases[i], &results[i]);
        }

        for (unsigned int i = 0; i < FAILURE_CASE_COUNT; i++) {
            report_case(&failure_cases[i], &results[i]);
        }
    }

    sddf_printf("FAILURE_BENCH|DONE\n");

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
