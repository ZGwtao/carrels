#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <elf.h>
#include <microkit.h>

#define SVC_TYPE_MAX_NUM (8)

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

typedef enum {
    
    PROTOCON_ACTIVE = 1,
    PROTOCON_PASSIVE,
    PROTOCON_HANG,

} protocon_lifecycle_state_t;


typedef struct {

    int num_svc_per_type[SVC_TYPE_MAX_NUM];

    // for each type of ossvc, there are at most SVC_PER_TYPE_MAX_NUM instances
    // for each instance, uintptr_t is the data word for them...

    seL4_Word data_per_svc_instance[SVC_TYPE_MAX_NUM][SVC_PER_TYPE_MAX_NUM];

} protocon_svc_req_t;

#define PROGNAME "[@monitor] "
#define PC_SVC_DESC_SECTION_NAME ".pc_svc_desc"
typedef struct {
    Elf64_Ehdr *header_payload;
    Elf64_Shdr *header_service_info;
} payload_info_t;


seL4_Error payload_info_parse(payload_info_t *info, uintptr_t base);


#define OSSVC_TYPE_COUNT 8
#define OSSVC_MAX_INSTANCES_PER_TYPE 8

typedef struct {
    uint8_t count[OSSVC_TYPE_COUNT];
    seL4_Word iface_addr
        [OSSVC_TYPE_COUNT]
        [OSSVC_MAX_INSTANCES_PER_TYPE];
} service_requirements_t;


void monitor_init_ossvc_map(monitor_svcdb_t *svcdb_list, int monitor_svc_dist_map[][SVC_TYPE_MAX_NUM]);


void service_manifest_parse(payload_info_t *payload, protocon_svc_req_t *req);


void service_installer_apply(
    int cid,
    protocon_svc_req_t *req,
    uintptr_t payload_base,
    uintptr_t monitor_svcdb_base,
    protocon_svcdb_t *svcdb
);


int service_registry_create(
        protocon_svc_req_t *req,
        protocon_lifecycle_state_t *protocon_states,
        int monitor_svc_dist_map[][SVC_TYPE_MAX_NUM]
);
