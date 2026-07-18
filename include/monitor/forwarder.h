#pragma once

#include <microkit.h>
#include <ioutils/pd_io_queue.h>


#define PD_IO_CLIENT_COUNT              4u
#define PD_IO_MONITOR_NOTIFY_BASE       40u
#define PD_IO_MONITOR_SLOT_BASE         0x80000000u
#define PD_IO_MONITOR_SLOT_SIZE         0x00400000u

#define PD_IO_CAPACITY                  512u
#define PD_IO_BUFFER_SIZE               2048u
#define PD_IO_DATA_SIZE                 (PD_IO_CAPACITY * PD_IO_BUFFER_SIZE)

#define PD_IO_CLIENT_TX_FREE_OFFSET     0x000000u
#define PD_IO_CLIENT_RX_FREE_OFFSET     0x003000u
#define PD_IO_CLIENT_TX_ACTIVE_OFFSET   0x006000u
#define PD_IO_CLIENT_RX_ACTIVE_OFFSET   0x009000u
#define PD_IO_CLIENT_TX_DATA_OFFSET     0x00C000u
#define PD_IO_CLIENT_RX_DATA_OFFSET     0x10C000u


void monitor_init_all_client_links(uint64_t pc_num);


void monitor_handle_client_payload(uint32_t sender_cid);


