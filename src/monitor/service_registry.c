
#include <ossvc.h>
#include <protocon.h>


void monitor_ossvc_populate_all_svc_of_unipd(protocon_svcdb_t *svcdb, int map[])
{
    protocon_svc_t *curr_svc;
    for (int i = 0; i < svcdb->svc_num; ++i) {
        /* Iterate all OS services of a PD */
        curr_svc = &svcdb->array[i];
        if (curr_svc->svc_init == false) {
            continue;
        }
        /* Determine what type the OS service is */
        int svc_type = curr_svc->svc_type;
        /* Check the number of OS service of the same type */
        int num_curr_type = map[svc_type];
        if (num_curr_type >= SVC_PER_TYPE_MAX_NUM) {
            microkit_dbg_puts("Too many OS services of the same type\n");
            microkit_internal_crash(-1);
        }
        /* Pin the OS service on the map */
        map[curr_svc->svc_type]++;
    }
}

// FIXME: we should pass the map as an argument to avoid the dependency on the global variable,
//  but it is not a big deal for now as we are still in the early stage of prototyping
void service_registry_create(monitor_svcdb_t *svcdb_list, int monitor_svc_dist_map[][SVC_TYPE_MAX_NUM])
{
    // we will populate the global map that records the distribution of OS services for each dynamic PD (protocon)
    // monitor_svcdb_t *svcdb_list = &monitor_svc_db;
    // for all dynamic PDs, try to populate the map with this loop
    for (int i = 0; i < svcdb_list->len; ++i) {
        // get the pointer to the OS service database of this PD,
        protocon_svcdb_t *curr_svcdb = &svcdb_list->list[i];
        // for each dynamic PD, we will populate the map with the information of all OS services of this PD
        monitor_ossvc_populate_all_svc_of_unipd(curr_svcdb, monitor_svc_dist_map[i]);
    }
}
