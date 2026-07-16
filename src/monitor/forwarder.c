#include <forwarder.h>
#include <sddf/util/printf.h>
#include <ossvc.h>

extern int pd_io_acl_rule;

static pd_io_link_t client_links[PD_IO_CLIENT_COUNT];

static void monitor_init_client_link(uint32_t cid)
{
    uintptr_t base = PD_IO_MONITOR_SLOT_BASE +
                     cid * PD_IO_MONITOR_SLOT_SIZE;

    /*
     * monitor RX == client TX
     */
    pd_io_direction_init(
        &client_links[cid].rx,
        (pd_io_queue_t *)(base + PD_IO_CLIENT_TX_FREE_OFFSET),
        (pd_io_queue_t *)(base + PD_IO_CLIENT_TX_ACTIVE_OFFSET),
        (void *)(base + PD_IO_CLIENT_TX_DATA_OFFSET),
        PD_IO_DATA_SIZE,
        PD_IO_CAPACITY,
        PD_IO_BUFFER_SIZE
    );

    /*
     * monitor TX == client RX
     */
    pd_io_direction_init(
        &client_links[cid].tx,
        (pd_io_queue_t *)(base + PD_IO_CLIENT_RX_FREE_OFFSET),
        (pd_io_queue_t *)(base + PD_IO_CLIENT_RX_ACTIVE_OFFSET),
        (void *)(base + PD_IO_CLIENT_RX_DATA_OFFSET),
        PD_IO_DATA_SIZE,
        PD_IO_CAPACITY,
        PD_IO_BUFFER_SIZE
    );

    /*
     * The monitor is the sole shared-state initialisation owner.
     * Do this before the corresponding client starts.
     */
    int err = pd_io_direction_reset_and_fill(&client_links[cid].rx);
    if (err != PD_IO_QUEUE_OK) {
        sddf_printf(PROGNAME "failed to initialise client %u RX: %d\n",
                    cid, err);
        microkit_internal_crash(err);
    }

    err = pd_io_direction_reset_and_fill(&client_links[cid].tx);
    if (err != PD_IO_QUEUE_OK) {
        sddf_printf(PROGNAME "failed to initialise client %u TX: %d\n",
                    cid, err);
        microkit_internal_crash(err);
    }
}

void monitor_init_all_client_links(void)
{
    for (uint32_t cid = 0; cid < PD_IO_CLIENT_COUNT; cid++) {
        monitor_init_client_link(cid);
    }
}

#define PD_IO_MONITOR_SOURCE_ID UINT8_MAX

static bool monitor_pd_can_receive(uint32_t cid)
{
    return cid < PD_IO_CLIENT_COUNT &&
           cid < PC_CHILD_PER_MONITOR_MAX_NUM &&
           !protocon_state_check_lifecycle_state(cid, PROTOCON_PASSIVE);
}

static int monitor_send_pong(uint32_t sender_cid)
{
    static const char pong[] = "pong from monitor";

    /*
     * The response is also a framed pd_io message.
     *
     * source          = monitor
     * bitmap_targets  = original sender
     * payload         = "pong from monitor"
     */
    uint8_t target_bitmap = (uint8_t)(1u << sender_cid);

    int err = pd_io_direction_send(
        &client_links[sender_cid].tx,
        PD_IO_MONITOR_SOURCE_ID,
        target_bitmap,
        pong,
        (uint32_t)sizeof(pong)
    );

    if (err != PD_IO_QUEUE_OK) {
        sddf_printf(
            PROGNAME "failed to send pong to client %u: %d\n",
            sender_cid,
            err
        );
        return err;
    }

    microkit_notify(PD_IO_MONITOR_NOTIFY_BASE + sender_cid);
    return PD_IO_QUEUE_OK;
}

static void monitor_forward_payload(uint32_t sender_cid,
                                    const pd_io_header_t *header,
                                    const void *payload,
                                    uint32_t payload_len)
{
    uint8_t requested_targets = header->bitmap_targets;

    for (uint32_t target_cid = 0;
         target_cid < PD_IO_CLIENT_COUNT;
         target_cid++) {

        if (target_cid == sender_cid) {
            continue;
        }
        if (pd_io_acl_rule) {
        #if 1
            if ((sender_cid % 2) != (target_cid % 2)) {
                continue;
            }
        } else {
            if ((sender_cid % 2) == (target_cid % 2)) {
                continue;
            }
        #endif
        }

        uint8_t target_bit = (uint8_t)(1u << target_cid);

        /*
         * The sender did not select this PD.
         */
        if ((requested_targets & target_bit) == 0) {
            continue;
        }

        /*
         * Do not route to a passive/uninstantiated PD.
         *
         * This follows the requested rule: any state other than
         * PROTOCON_PASSIVE is considered routable.
         */
        if (!monitor_pd_can_receive(target_cid)) {
            sddf_printf(
                PROGNAME
                "not forwarding client %u message to passive client %u\n",
                sender_cid,
                target_cid
            );
            continue;
        }

        /*
         * Preserve the authenticated sender ID rather than trusting
         * header->source supplied by the client.
         *
         * Preserve the original target bitmap so the receiver can see
         * the complete intended recipient set.
         */
        int err = pd_io_direction_send(
            &client_links[target_cid].tx,
            (uint8_t)sender_cid,
            requested_targets,
            payload,
            payload_len
        );

        if (err != PD_IO_QUEUE_OK) {
            sddf_printf(
                PROGNAME
                "failed to forward from client %u to client %u: %d\n",
                sender_cid,
                target_cid,
                err
            );
            continue;
        }

        microkit_notify(PD_IO_MONITOR_NOTIFY_BASE + target_cid);

        sddf_printf(
            PROGNAME
            "forwarded %u bytes from client %u to client %u\n",
            payload_len,
            sender_cid,
            target_cid
        );
    }
}

void monitor_handle_client_payload(uint32_t sender_cid)
{
    pd_io_header_t header;
    uint8_t payload[PD_IO_BUFFER_SIZE];
    uint32_t payload_len;

    if (sender_cid >= PD_IO_CLIENT_COUNT) {
        sddf_printf(
            PROGNAME "invalid sender client ID: %u\n",
            sender_cid
        );
        return;
    }

    for (;;) {
        int err = pd_io_direction_receive(
            &client_links[sender_cid].rx,
            &header,
            payload,
            sizeof(payload),
            &payload_len
        );

        if (err == PD_IO_QUEUE_EMPTY) {
            break;
        }

        if (err != PD_IO_QUEUE_OK) {
            sddf_printf(
                PROGNAME "client %u receive failed: %d\n",
                sender_cid,
                err
            );

            /*
             * A malformed descriptor/header has already been recycled
             * by pd_io_direction_receive(). Continue draining later
             * messages instead of abandoning the queue.
             */
            continue;
        }

        /*
         * Do not trust a client-provided source field for routing.
         * The channel that delivered the notification identifies the
         * actual sender.
         */
        if (header.source != (uint8_t)sender_cid) {
            sddf_printf(
                PROGNAME
                "client %u supplied mismatched source ID %u; "
                "using channel-derived sender ID\n",
                sender_cid,
                header.source
            );
        }
        assert(header.payload_size == payload_len);

        /*
         * A framed message with zero payload is interpreted as a ping.
         */
        if (payload_len == 0) {
            (void)monitor_send_pong(sender_cid);
            continue;
        }

        monitor_forward_payload(
            sender_cid,
            &header,
            payload,
            payload_len
        );
    }
}
