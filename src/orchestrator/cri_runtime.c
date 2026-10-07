/* SPDX-License-Identifier: BSD-2-Clause */
#include "cri_runtime.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <microkit.h>
#include <sddf/util/printf.h>
#include <libvmm/virtio/vsock_config.h>
#include <libvmm/virtio/vsock_queue.h>

#define CRI_PORT 1234U
#define CRI_MAGIC 0x4352564dU
#define CRI_VERSION 1U
#define CRI_MAX_OBJECTS 32U
#define VSOCK_HOST_CID 2U
#define VSOCK_TYPE_STREAM 1U
#define VSOCK_OP_REQUEST 1U
#define VSOCK_OP_RESPONSE 2U
#define VSOCK_OP_RST 3U
#define VSOCK_OP_SHUTDOWN 4U
#define VSOCK_OP_RW 5U
#define VSOCK_OP_CREDIT_UPDATE 6U
#define VSOCK_OP_CREDIT_REQUEST 7U
#define CARRELS_IMAGE_SCHEME "carrels-image://"

enum cri_op {
    CRI_HELLO = 1,
    CRI_RUN_SANDBOX = 10, CRI_STOP_SANDBOX, CRI_REMOVE_SANDBOX,
    CRI_SANDBOX_STATUS, CRI_LIST_SANDBOX,
    CRI_CREATE_CONTAINER = 20, CRI_START_CONTAINER, CRI_STOP_CONTAINER,
    CRI_REMOVE_CONTAINER, CRI_CONTAINER_STATUS, CRI_LIST_CONTAINER,
    CRI_REOPEN_LOG,
    CRI_PULL_IMAGE = 30, CRI_REMOVE_IMAGE, CRI_IMAGE_STATUS, CRI_LIST_IMAGE,
};
enum cri_result { CRI_OK, CRI_NOT_FOUND, CRI_INVALID, CRI_FULL };

struct __attribute__((packed)) cri_object {
    uint32_t cursor, present, attempt, state, exit_code;
    uint32_t network_ns, ipc_ns, pid_ns;
    uint64_t created_at, started_at, finished_at;
    char id[64], name[128], namespace_[64], uid[64];
    uint8_t labels[512], annotations[512];
    char runtime_handler[64], log_dir[192], sandbox_id[64];
    char image[256], image_ref[256], log_path[192], reason[64], ip[48];
};
struct __attribute__((packed)) rpc_header {
    uint32_t magic;
    uint16_t version, op;
    uint32_t request_id, status, length;
};
struct __attribute__((packed)) vsock_hdr {
    uint64_t src_cid, dst_cid;
    uint32_t src_port, dst_port, len;
    uint16_t type, op;
    uint32_t flags, buf_alloc, fwd_cnt;
};

extern virtio_vsock_transport_config_t vsock_config;
static virtio_vsock_queue_handle_t transport;
static struct cri_object sandboxes[CRI_MAX_OBJECTS];
static struct cri_object containers[CRI_MAX_OBJECTS];
static struct cri_object images[CRI_MAX_OBJECTS];
static uint64_t next_id, timestamp;
static bool connected;
static uint64_t peer_cid;
static uint32_t peer_port, tx_count, rx_fwd_cnt;
static uint8_t stream[sizeof(struct rpc_header) + sizeof(struct cri_object)];
static uint32_t stream_len;
static struct cri_object *start_after_reply;

static void copy_string(char *dst, size_t size, const char *src)
{
    size_t len;
    if (!size) return;
    len = strlen(src);
    if (len >= size) len = size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void set_image_ref(struct cri_object *o)
{
    if (strncmp(o->image, CARRELS_IMAGE_SCHEME,
                sizeof(CARRELS_IMAGE_SCHEME) - 1) == 0) {
        copy_string(o->image_ref, sizeof(o->image_ref), o->image);
    } else {
        sddf_snprintf(o->image_ref, sizeof(o->image_ref), "%s%s",
                      CARRELS_IMAGE_SCHEME, o->image);
    }
}

static struct cri_object *find(struct cri_object *table, const char *id)
{
    for (unsigned i = 0; i < CRI_MAX_OBJECTS; i++)
        if (table[i].present && !strcmp(table[i].id, id)) return &table[i];
    return NULL;
}

static struct cri_object *allocate(struct cri_object *table)
{
    for (unsigned i = 0; i < CRI_MAX_OBJECTS; i++) {
        if (!table[i].present) {
            memset(&table[i], 0, sizeof(table[i]));
            table[i].present = 1;
            return &table[i];
        }
    }
    return NULL;
}

static uint32_t list_one(struct cri_object *table, uint32_t cursor,
                         struct cri_object *response)
{
    for (unsigned i = cursor; i < CRI_MAX_OBJECTS; i++) {
        if (table[i].present) {
            *response = table[i];
            response->cursor = i + 1;
            return CRI_OK;
        }
    }
    response->cursor = CRI_MAX_OBJECTS;
    return CRI_OK;
}

static uint32_t dispatch(uint16_t op, const struct cri_object *request,
                         struct cri_object *response)
{
    struct cri_object *o;
    memset(response, 0, sizeof(*response));
    switch (op) {
    case CRI_HELLO:
        response->present = 1; return CRI_OK;
    case CRI_RUN_SANDBOX:
        o = allocate(sandboxes); if (!o) return CRI_FULL;
        *o = *request; o->present = 1;
        sddf_snprintf(o->id, sizeof(o->id), "carrels-sandbox-%08lx", ++next_id);
        o->created_at = ++timestamp; copy_string(o->ip, sizeof(o->ip), "192.0.2.1");
        copy_string(o->reason, sizeof(o->reason), "CarrelsReady"); *response = *o;
        return CRI_OK;
    case CRI_STOP_SANDBOX:
        o = find(sandboxes, request->id); if (o) o->state = 1; return CRI_OK;
    case CRI_REMOVE_SANDBOX:
        o = find(sandboxes, request->id); if (o) memset(o, 0, sizeof(*o)); return CRI_OK;
    case CRI_SANDBOX_STATUS:
        o = find(sandboxes, request->id); if (!o) return CRI_NOT_FOUND; *response = *o; return CRI_OK;
    case CRI_LIST_SANDBOX: return list_one(sandboxes, request->cursor, response);
    case CRI_CREATE_CONTAINER:
        o = allocate(containers); if (!o) return CRI_FULL;
        *o = *request; o->present = 1;
        sddf_snprintf(o->id, sizeof(o->id), "carrels-container-%08lx", ++next_id);
        o->created_at = ++timestamp;
        set_image_ref(o);
        copy_string(o->reason, sizeof(o->reason), "CarrelsCreated"); *response = *o;
        return CRI_OK;
    case CRI_START_CONTAINER:
        o = find(containers, request->id); if (!o) return CRI_NOT_FOUND;
        /* CRI requires StartContainer to expose RUNNING to kubelet.  The
         * deployment callback separately verifies that state against the
         * monitor and turns this into FAILED if the protocon is not active. */
        o->state = 1; o->started_at = ++timestamp;
        copy_string(o->reason, sizeof(o->reason), "CarrelsStarting");
        start_after_reply = o;
        return CRI_OK;
    case CRI_STOP_CONTAINER:
        o = find(containers, request->id); if (!o) return CRI_NOT_FOUND;
        if (o->state == 1 && orchestrator_cri_stop(o->network_ns)) return CRI_INVALID;
        o->state = 2; o->finished_at = ++timestamp;
        copy_string(o->reason, sizeof(o->reason), "CarrelsStopped"); return CRI_OK;
    case CRI_REMOVE_CONTAINER:
        o = find(containers, request->id); if (o) memset(o, 0, sizeof(*o)); return CRI_OK;
    case CRI_CONTAINER_STATUS:
        o = find(containers, request->id); if (!o) return CRI_NOT_FOUND; *response = *o; return CRI_OK;
    case CRI_LIST_CONTAINER: return list_one(containers, request->cursor, response);
    case CRI_REOPEN_LOG: return find(containers, request->id) ? CRI_OK : CRI_NOT_FOUND;
    case CRI_PULL_IMAGE:
        if (!orchestrator_cri_image_exists(request->image)) return CRI_NOT_FOUND;
        for (unsigned i = 0; i < CRI_MAX_OBJECTS; i++)
            if (images[i].present && !strcmp(images[i].image, request->image)) { *response = images[i]; return CRI_OK; }
        o = allocate(images); if (!o) return CRI_FULL; *o = *request; o->present = 1;
        set_image_ref(o);
        copy_string(o->id, sizeof(o->id), o->image_ref); *response = *o; return CRI_OK;
    case CRI_REMOVE_IMAGE:
        o = find(images, request->id); if (o) memset(o, 0, sizeof(*o)); return CRI_OK;
    case CRI_IMAGE_STATUS:
        o = find(images, request->id);
        if (!o) for (unsigned i = 0; i < CRI_MAX_OBJECTS; i++)
            if (images[i].present && !strcmp(images[i].image, request->image)) { o = &images[i]; break; }
        if (!o) return CRI_NOT_FOUND; *response = *o; return CRI_OK;
    case CRI_LIST_IMAGE: return list_one(images, request->cursor, response);
    default: return CRI_INVALID;
    }
}

static bool send_packet(uint16_t op, const void *payload, uint32_t len)
{
    uint8_t *packet = virtio_vsock_queue_tx_buffer(&transport);
    struct vsock_hdr h = {
        .src_cid = VSOCK_HOST_CID, .dst_cid = peer_cid,
        .src_port = CRI_PORT, .dst_port = peer_port, .len = len,
        .type = VSOCK_TYPE_STREAM, .op = op,
        .buf_alloc = sizeof(stream), .fwd_cnt = rx_fwd_cnt,
    };
    if (!packet || sizeof(h) + len > transport.buffer_size) return false;
    memcpy(packet, &h, sizeof(h));
    if (len) memcpy(packet + sizeof(h), payload, len);
    if (virtio_vsock_queue_enqueue(&transport, sizeof(h) + len)) return false;
    if (op == VSOCK_OP_RW) tx_count += len;
    microkit_notify(vsock_config.connection.id);
    return true;
}

static void process_stream(void)
{
    struct rpc_header request_header, response_header;
    struct cri_object request, response;
    if (stream_len < sizeof(request_header)) return;
    memcpy(&request_header, stream, sizeof(request_header));
    if (request_header.magic != CRI_MAGIC || request_header.version != CRI_VERSION ||
        request_header.length != sizeof(request)) {
        stream_len = 0; (void)send_packet(VSOCK_OP_RST, NULL, 0); connected = false; return;
    }
    if (stream_len < sizeof(request_header) + sizeof(request)) return;
    memcpy(&request, stream + sizeof(request_header), sizeof(request));
    response_header = request_header;
    response_header.status = dispatch(request_header.op, &request, &response);
    response_header.length = sizeof(response);
    uint8_t reply[sizeof(response_header) + sizeof(response)];
    memcpy(reply, &response_header, sizeof(response_header));
    memcpy(reply + sizeof(response_header), &response, sizeof(response));
    bool sent = send_packet(VSOCK_OP_RW, reply, sizeof(reply));
    if (request_header.op == CRI_START_CONTAINER ||
        request_header.op == CRI_STOP_CONTAINER) {
        sddf_printf("[@vsock_backend] CRI op=%u request=%u status=%u reply=%s\n",
                    request_header.op, request_header.request_id,
                    response_header.status, sent ? "queued" : "dropped");
    }
    stream_len = 0;
    if (request_header.op == CRI_START_CONTAINER &&
        response_header.status == CRI_OK && start_after_reply != NULL) {
        struct cri_object *o = start_after_reply;
        uint32_t pc_id;
        start_after_reply = NULL;
        if (orchestrator_cri_start(o->image, &pc_id)) {
            o->state = 2;
            o->exit_code = 1;
            o->finished_at = ++timestamp;
            copy_string(o->reason, sizeof(o->reason), "CarrelsStartFailed");
        } else {
            o->network_ns = pc_id;
        }
    }
}

static void process_packet(const void *packet, uint32_t packet_len)
{
    struct vsock_hdr h;
    if (packet_len < sizeof(h)) return;
    memcpy(&h, packet, sizeof(h));
    if (h.len > packet_len - sizeof(h) || h.dst_cid != VSOCK_HOST_CID || h.type != VSOCK_TYPE_STREAM) return;
    if (h.op == VSOCK_OP_REQUEST) {
        if (connected || h.dst_port != CRI_PORT) return;
        connected = true; peer_cid = h.src_cid; peer_port = h.src_port;
        stream_len = 0; (void)send_packet(VSOCK_OP_RESPONSE, NULL, 0);
        sddf_printf("[@vsock_backend] CRI shim connected\n"); return;
    }
    if (!connected || h.src_cid != peer_cid || h.src_port != peer_port || h.dst_port != CRI_PORT) return;
    if (h.op == VSOCK_OP_RW) {
        if (h.len <= sizeof(stream) - stream_len) {
            memcpy(stream + stream_len, (const uint8_t *)packet + sizeof(h), h.len);
            stream_len += h.len; rx_fwd_cnt += h.len; process_stream();
        }
    } else if (h.op == VSOCK_OP_CREDIT_REQUEST) {
        (void)send_packet(VSOCK_OP_CREDIT_UPDATE, NULL, 0);
    } else if (h.op == VSOCK_OP_SHUTDOWN || h.op == VSOCK_OP_RST) {
        if (h.op == VSOCK_OP_SHUTDOWN) (void)send_packet(VSOCK_OP_RST, NULL, 0);
        connected = false; stream_len = 0;
    }
}

void cri_runtime_init(void)
{
    virtio_vsock_connection_resource_t *c = &vsock_config.connection;
    virtio_vsock_queue_init(&transport, c->tx_queue.vaddr, c->tx_data.vaddr,
                            c->rx_queue.vaddr, c->rx_data.vaddr,
                            c->capacity, c->buffer_size);
    sddf_printf("[@vsock_backend] CRI runtime listening on CID 2 port %u\n", CRI_PORT);
}

void cri_runtime_notified(void)
{
    void *packet; uint32_t len; bool consumed = false;
    while (!virtio_vsock_queue_peek(&transport, &packet, &len)) {
        process_packet(packet, len); (void)virtio_vsock_queue_dequeue(&transport); consumed = true;
    }
    if (consumed) microkit_notify(vsock_config.connection.id);
}

void cri_runtime_deploy_complete(uint32_t pc_id, int result)
{
    for (unsigned i = 0; i < CRI_MAX_OBJECTS; i++) {
        struct cri_object *o = &containers[i];
        if (o->present && o->state == 1 && o->network_ns == pc_id &&
            !strcmp(o->reason, "CarrelsStarting")) {
            copy_string(o->reason, sizeof(o->reason), result ? "CarrelsStartFailed" : "CarrelsRunning");
            if (result) {
                o->state = 2; o->exit_code = 1; o->finished_at = ++timestamp;
            }
            return;
        }
    }
}
