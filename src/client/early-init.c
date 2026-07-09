
#include <libtrustedlo.h>
#include <tsldr_vm_layout.h>


__attribute__((constructor))
void register_app_early_init(void)
{
    trampoline_args_t *args = (trampoline_args_t *)tsldr_vm_layout.trampoline_args.base;
    client_args_t *client_args =
        (client_args_t *)((unsigned char *)args + sizeof(trampoline_args_t));

    microkit_pps = client_args->bitmap_ppcs;
    microkit_irqs = client_args->bitmap_irqs;
    microkit_ioports = client_args->bitmap_ioports;
    microkit_notifications = client_args->bitmap_notifications;
    tsldr_miscutil_memcpy(microkit_name,
                          client_args->dynamic_pd_name,
                          sizeof(client_args->dynamic_pd_name));
}

