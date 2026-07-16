
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <elf.h>
#include <microkit.h>
#include <libtrustedlo.h>

#define SVC_TYPE_MAX_NUM (8)

#define PC_CHILD_PER_MONITOR_MAX_NUM (16)

#define SVC_PER_TYPE_MAX_NUM (8)

typedef struct {
    seL4_Word vaddr;
    seL4_Word page_num;
    seL4_Word page_size;
} svc_mapping_t;

typedef struct {
    // whether or not a valid os service
    bool svc_init;
    // corresponding to the XML gid
    uint8_t svc_idx;
    // the type of this os service
    uint8_t svc_type;
    // notifications
    uint8_t notifications[4];
    // ppcs
    uint8_t ppcs[4];
    // irqs
    uint8_t irqs[4];
    // ioports
    uint8_t ioports[4];
    // mappings
    svc_mapping_t mappings[4];
    // data_path
    char data_path[64];
} protocon_svc_t;

typedef struct {
    // specify which PD this array belongs to
    uint8_t pd_idx;
    // number of available os services in the array
    uint8_t svc_num;
    // array of os services of this PD
    protocon_svc_t array[16];
} protocon_svcdb_t;

typedef struct {
    // overall length of this region
    size_t len;
    // list of os service arrays
    protocon_svcdb_t list[16];
} monitor_svcdb_t;

typedef uint8_t protocon_lifecycle_state_t;
enum {
    PROTOCON_ACTIVE = 1,
    PROTOCON_PASSIVE,
    PROTOCON_HANG,
};
_Static_assert(sizeof(protocon_lifecycle_state_t) == sizeof(uint8_t),
               "protocon_lifecycle_state_t must be uint8_t");

typedef struct __attribute__((packed)) {
    uint64_t magic;
    uint32_t service_count;
    uint32_t manifest_size;
    uint64_t elf_size;
} service_manifest_header_t;

#define MANIFEST_MAGIC UINT64_C(0x504353564D414E31)

typedef struct __attribute__((packed)) {
    int32_t type;
    uint64_t offset;
    uint64_t size;
} service_manifest_entry_t;

_Static_assert(sizeof(service_manifest_header_t) == 24,
               "Invalid manifest header size");
_Static_assert(sizeof(service_manifest_entry_t) == 20,
               "Invalid manifest entry size");

#define PROGNAME "[@monitor] "


typedef struct {
    uint32_t service_count;
    service_manifest_entry_t *service_entries[16];
    const protocon_svc_t *service_sources[16];
    Elf64_Addr payload_e_entry;

    uint8_t num_svc_per_type[SVC_TYPE_MAX_NUM];
    seL4_Word data_per_svc_instance[SVC_TYPE_MAX_NUM][SVC_PER_TYPE_MAX_NUM];

} protocon_svc_req_t;



typedef struct {
    Elf64_Ehdr *header_payload;
    uint64_t elf_payload_size;
    uint32_t service_count;
    service_manifest_entry_t *service_entries;
} payload_info_t;


seL4_Error payload_info_parse(payload_info_t *info, uintptr_t base);

typedef struct {
    uint32_t pc_id;
    uintptr_t pc_base;
    Elf64_Addr pc_entry;
    protocon_svc_req_t *req;
} deploy_plan_t;

static inline
void deploy_plan_reset(deploy_plan_t *p)
{
    p->pc_id = PC_CHILD_PER_MONITOR_MAX_NUM;
    p->pc_base = 0x0;
    p->pc_entry = 0x0;
    p->req = NULL;
}


typedef struct pc_state {
    uint32_t pc_id;
    tsldr_context_t context;
    protocon_lifecycle_state_t life_cycle_state;
    uint32_t avail_service_per_type[SVC_TYPE_MAX_NUM];
    const protocon_svc_t *avail_service_refs[SVC_TYPE_MAX_NUM][SVC_PER_TYPE_MAX_NUM];
} pc_state_t;


extern pc_state_t protocon_states[PC_CHILD_PER_MONITOR_MAX_NUM];


static inline tsldr_context_t *
protocon_state_retrieve_context(uint32_t pc_id)
{
    if (pc_id >= PC_CHILD_PER_MONITOR_MAX_NUM) {
        TSLDR_DBG_PRINT(
            PROGNAME
            "Invalid cid given for retrieving context from protocon_states\n"
        );
        return NULL;
    }
    return &(protocon_states[pc_id].context);
}

static inline void
protocon_state_set_lifecycle_state(
    uint32_t pc_id,
    protocon_lifecycle_state_t state
) {
    protocon_states[pc_id].life_cycle_state = state;
}

static inline protocon_lifecycle_state_t
protocon_state_get_lifecycle_state(uint32_t pc_id)
{
    return protocon_states[pc_id].life_cycle_state;
}

static inline bool
protocon_state_check_lifecycle_state(
    uint32_t pc_id,
    protocon_lifecycle_state_t state
) {
    return protocon_state_get_lifecycle_state(pc_id) == state;
}

static inline void
protocon_state_memzero_services(uint32_t pc_id)
{
    pc_state_t *state = &protocon_states[pc_id];

    for (uint32_t i = 0; i < SVC_TYPE_MAX_NUM; ++i) {
        tsldr_miscutil_memset(
            state->avail_service_refs[i],
            0,
            (SVC_PER_TYPE_MAX_NUM) * sizeof(protocon_svc_t *)
        );
        state->avail_service_per_type[i] = 0;
    }
}

static inline void
protocon_state_memzero_context(uint32_t pc_id)
{
    pc_state_t *state = &protocon_states[pc_id];
    tsldr_miscutil_memset(
        &state->context,
        0,
        sizeof(tsldr_context_t)
    );
}

void service_registry_create(const monitor_svcdb_t *svcdb_list, pc_state_t *protocon_states);


void service_manifest_parse(payload_info_t *payload, protocon_svc_req_t *req);


void service_installer_apply(
    const deploy_plan_t *plan,
    uintptr_t monitor_svcdb_base
);


void service_planner_select_protocon(
    protocon_svc_req_t *req,
    deploy_plan_t *plan,
    const pc_state_t *protocon_states
);
