
#include <ossvc.h>
#include <protocon.h>
#include <libtrustedlo.h>


static inline void monitor_ossvc_init_req_per_type(protocon_svc_req_t *req, protocon_svc_type_t svc_type, uint8_t svc_num_per_type, seL4_Word svc_data_list[])
{
    switch(svc_type) {
    // these OS services are what we support for now, but can extend later
    // possibly more than eight types of OS services?
    // but if an application requires more OS service types than 8,
    // probably it is a sign that the application is too heavy to be put in a dynamic PD
    case FS_IFACE:
    case TIMER_IFACE:
    case SERIAL_IFACE: {
        for (uint8_t i = 0; i < svc_num_per_type; ++i) {
            seL4_Word svc_data = svc_data_list[i];
            req->data_per_svc_instance[svc_type][i] = svc_data;
        }
        break;
    }
    default:
        TSLDR_DBG_PRINT("Unsupported SVC type: %d\n", svc_type);
        break;
    };
}

void monitor_ossvc_parse_req_from_elf_section(void *elf_base, void *sh, protocon_svc_req_t *req)
{
    // parse the interface section ...
    // i.e., get the user-defined section for declaring what OS services are requested
    protocon_svc_desc_t *ib = (protocon_svc_desc_t *)(elf_base + (uint64_t)((Elf64_Shdr *)sh)->sh_offset);

    // the list of numbers of requested OS services
    const uint8_t *svc_req_num_per_type = &ib->t1_num;
    // the corresponding types which map to the above list of numbers
    const protocon_svc_type_t *svc_req_types = &ib->type1;

    seL4_Word (*svc_per_type_data_map[PC_SVC_TYPE_MAX_NUM])[PC_SVC_PER_PD_MAX_NUM] = {
        &ib->t1_iface, &ib->t2_iface, &ib->t3_iface, &ib->t4_iface,
        &ib->t5_iface, &ib->t6_iface, &ib->t7_iface, &ib->t8_iface
    };

    for (int i = 0; i < PC_SVC_TYPE_MAX_NUM; ++i) {
        if (svc_req_num_per_type[i] == 0) { // pass...
            continue;
        }
        protocon_svc_type_t curr_type = svc_req_types[i];

        // fetch the number of svc requested...
        uint8_t n = svc_req_num_per_type[i] > PC_SVC_PER_PD_MAX_NUM ? PC_SVC_PER_PD_MAX_NUM : svc_req_num_per_type[i];
        req->num_svc_per_type[curr_type] = n;

        // for each of the OS service, turns it into an OS service request,
        // which will then go into requests of low-level access rights
        seL4_Word *svc_data_list = *svc_per_type_data_map[i];
        monitor_ossvc_init_req_per_type(req, curr_type, n, svc_data_list);
    }
}
