#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (C) 2020 Bin Meng <bmeng.cn@gmail.com>
#

libsbiutils-objs-$(CONFIG_FDT) += fdt/fdt_early_drivers.carray.o

libsbiutils-objs-$(CONFIG_FDT_DOMAIN) += fdt/fdt_domain.o
libsbiutils-objs-$(CONFIG_FDT_PMU) += fdt/fdt_pmu.o
libsbiutils-objs-$(CONFIG_FDT) += fdt/fdt_helper.o
libsbiutils-objs-$(CONFIG_FDT) += fdt/fdt_driver.o
libsbiutils-objs-$(CONFIG_FDT) += fdt/fdt_fixup.o

carray-fdt_early_drivers-$(CONFIG_FDT_BOOT_HARTID) += fdt_boot_hartid
libsbiutils-objs-$(CONFIG_FDT_BOOT_HARTID) += fdt/fdt_boot_hartid.o