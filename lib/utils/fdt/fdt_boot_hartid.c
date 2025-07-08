#include <libfdt.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_init.h>
#include <sbi_utils/fdt/fdt_driver.h>

static int sbi_setup_boot_hartid(const void *fdt, int nodeoff, 
                    const struct fdt_match *match)
{
    int cpus_node;
    cpus_node = fdt_path_offset(fdt, "/chosen");
    if (cpus_node < 0) {
        sbi_printf("Error: failed to find /cpus node in device tree! (Error: %s)\n", fdt_strerror(cpus_node));
        return SBI_ERR_NOT_SUPPORTED;
    }

    int ret = fdt_setprop_u32((void *)fdt, cpus_node,
                            "boot-hartid",
                            get_boot_hartid());
    if (ret < 0) {
        sbi_printf("Error: Failed to set 'boot-hartid' property! (Error: %s)\n", fdt_strerror(ret));
        return SBI_EFAIL;
    }

    return SBI_SUCCESS;
}

static const struct fdt_match boot_hartid_match[] = {
	{ .compatible = "boot-hartid" },
	{},
};

const struct fdt_driver fdt_boot_hartid = {
	.match_table = boot_hartid_match,
	.init = sbi_setup_boot_hartid,
};
