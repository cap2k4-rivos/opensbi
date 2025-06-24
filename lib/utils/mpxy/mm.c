#include <sbi_utils/mpxy/mm.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_mpxy.h>
#include <libfdt.h>
#include <sbi_utils/mpxy/fdt_mpxy.h>
#include <sbi/sbi_domain.h>

int mm_setup_bootinfo(const void *fdt, int nodeoff, const struct fdt_match *match,
                                u32 *out_channel_id, u32 *out_channel_server_id)
{
    const u32 *prop_instance, *prop_value;
    u64 base64, size64;
    char name[64];
    int i, len, offset;

    struct mm_boot_args *boot_args = NULL;
    struct mm_boot_info *boot_info = NULL;
    struct sbi_domain *tdomain = NULL;

    /* Get the phandle to the trusted domain instance associated with this channel. */
    prop_instance = fdt_getprop(fdt, nodeoff, "tdomain-instance", &len);
    if (!prop_instance || len < 4) {
        return SBI_EINVAL;
    }

    /* Find the device tree node offset corresponding to the trusted domain phandle. */
    offset = fdt_node_offset_by_phandle(fdt, fdt32_to_cpu(*prop_instance));
    if (offset < 0) {
        return SBI_EINVAL;
    }

    /* Retrieve the name of the trusted domain from its device tree node. */
    sbi_memset(name, 0, 64);
    strncpy(name, fdt_get_name(fdt, offset, NULL), sizeof(name) - 1);

    /* Get the domain structure corresponding to the trusted domain name. */
    tdomain = get_domain(name);
    if (NULL == tdomain)
        return SBI_EINVAL;

    /* The boot arguments for the trusted domain are passed via the domain structure. */
    boot_args = (void *)tdomain->next_arg1;
    boot_info = &boot_args->boot_info;

    /* Parse the number of memory regions from the device tree. */
    prop_value = fdt_getprop(fdt, nodeoff, "num-regions", &len);
    if (!prop_value || len < 4)
        return SBI_EINVAL;
    boot_info->num_mem_region = (unsigned int)fdt32_to_cpu(*prop_value);

    /* Parse the main memory region's base and size. */
    prop_value = fdt_getprop(fdt, nodeoff, "memory-reg", &len);
    if (!prop_value || len < 16)
        return SBI_EINVAL;
    base64 = fdt32_to_cpu(prop_value[0]);
    base64 = (base64 << 32) | fdt32_to_cpu(prop_value[1]);
    size64 = fdt32_to_cpu(prop_value[2]);
    size64 = (size64 << 32) | fdt32_to_cpu(prop_value[3]);
    boot_info->mm_mem_base  = base64;
    boot_info->mm_mem_limit = base64 + size64;

    /* Parse the image memory region's base and size. */
    prop_value = fdt_getprop(fdt, nodeoff, "image-reg", &len);
    if (!prop_value || len < 16)
        return SBI_EINVAL;
    base64 = fdt32_to_cpu(prop_value[0]);
    base64 = (base64 << 32) | fdt32_to_cpu(prop_value[1]);
    size64 = fdt32_to_cpu(prop_value[2]);
    size64 = (size64 << 32) | fdt32_to_cpu(prop_value[3]);
    boot_info->mm_image_base = base64;
    boot_info->mm_image_size = size64;

    /* Parse the heap region's base and size. */
    prop_value = fdt_getprop(fdt, nodeoff, "heap-reg", &len);
    if (!prop_value || len < 16)
        return SBI_EINVAL;
    base64 = fdt32_to_cpu(prop_value[0]);
    base64 = (base64 << 32) | fdt32_to_cpu(prop_value[1]);
    size64 = fdt32_to_cpu(prop_value[2]);
    size64 = (size64 << 32) | fdt32_to_cpu(prop_value[3]);
    boot_info->mm_heap_base = base64;
    boot_info->mm_heap_size = size64;

    /* Parse the stack region's base and size, and calculate the top of the stack. */
    prop_value = fdt_getprop(fdt, nodeoff, "stack-reg", &len);
    if (!prop_value || len < 16)
        return SBI_EINVAL;
    base64 = fdt32_to_cpu(prop_value[0]);
    base64 = (base64 << 32) | fdt32_to_cpu(prop_value[1]);
    size64 = fdt32_to_cpu(prop_value[2]);
    size64 = (size64 << 32) | fdt32_to_cpu(prop_value[3]);
    /* Stack grows downwards, so the base is the highest address. */
    boot_info->mm_stack_base = base64 + size64 - 1;

    /* Parse the per-CPU stack size. */
    prop_value = fdt_getprop(fdt, nodeoff, "pcpu-stack-size", &len);
    if (!prop_value || len < 4)
        return SBI_EINVAL;
    boot_info->mm_pcpu_stack_size = (unsigned long)fdt32_to_cpu(*prop_value);

    /* Parse the non-secure communication buffer's base and size. */
    prop_value = fdt_getprop(fdt, nodeoff, "ns-comm-buf", &len);
    if (!prop_value || len < 16)
        return SBI_EINVAL;
    base64 = fdt32_to_cpu(prop_value[0]);
    base64 = (base64 << 32) | fdt32_to_cpu(prop_value[1]);
    size64 = fdt32_to_cpu(prop_value[2]);
    size64 = (size64 << 32) | fdt32_to_cpu(prop_value[3]);
    boot_info->mm_ns_comm_buf_base = base64;
    boot_info->mm_ns_comm_buf_size = size64;

    /* Populate CPU information for the management mode. */
    boot_info->num_cpus = 0;
    /* Iterate over each hart (hardware thread) that this domain can possibly run on. */
    sbi_hartmask_for_each_hartindex(i, tdomain->possible_harts) {
        boot_args->cpu_info[i].linear_id = sbi_hartindex_to_hartid(i);
        boot_args->cpu_info[i].flags = 0;
        boot_info->num_cpus += 1;
    }
    /* Point the boot_info's cpu_info to the populated array in boot_args. */
    boot_info->cpu_info = boot_args->cpu_info;

    /**
     * Parse the Channel ID of the MPXY channel. This is the ID through which
     * the Management Mode will communicate.
     */
    prop_value = fdt_getprop(fdt, nodeoff, "riscv,sbi-mpxy-channel-id", &len);
    if (!prop_value || len < 4)
        return SBI_EINVAL;
    *out_channel_id = (unsigned int)fdt32_to_cpu(*prop_value);
    boot_info->mm_channel_id = *out_channel_id;

    /**
     * Parse the Server Channel ID. This is the channel to which control is
     * forwarded when the Management Mode sends a MM_COMMUNICATE request.
     */
    prop_value = fdt_getprop(fdt, nodeoff, "riscv,sbi-mpxy-channel-server-id", &len);
    if (!prop_value || len < 4)
        return SBI_EINVAL;
    *out_channel_server_id = (unsigned int)fdt32_to_cpu(*prop_value);

    return 0;
}
