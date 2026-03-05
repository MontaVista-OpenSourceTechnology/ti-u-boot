// SPDX-License-Identifier: GPL-2.0+
/*
 * Generic MMIO eFuse/register reader
 *
 * Copyright (C) 2025 Texas Instruments Incorporated - https://www.ti.com/
 */

#include <dm.h>
#include <misc.h>
#include <asm/io.h>

struct mmio_efuse_plat {
	void __iomem *base;
};

static int mmio_efuse_read(struct udevice *dev, int offset,
			   void *buf, int size)
{
	struct mmio_efuse_plat *plat = dev_get_plat(dev);
	u8 *dst = buf;
	int i;

	for (i = 0; i < size; i += sizeof(u32)) {
		u32 val = readl(plat->base + offset + i);
		int chunk = min((int)(size - i), (int)sizeof(u32));

		memcpy(dst + i, &val, chunk);
	}

	return size;
}

static int mmio_efuse_of_to_plat(struct udevice *dev)
{
	struct mmio_efuse_plat *plat = dev_get_plat(dev);

	plat->base = dev_read_addr_ptr(dev);
	if (!plat->base)
		return -EINVAL;

	return 0;
}

static const struct misc_ops mmio_efuse_ops = {
	.read = mmio_efuse_read,
};

static const struct udevice_id mmio_efuse_ids[] = {
	{ .compatible = "socionext,uniphier-efuse" },
	{ }
};

U_BOOT_DRIVER(mmio_efuse) = {
	.name		= "mmio_efuse",
	.id		= UCLASS_MISC,
	.of_match	= mmio_efuse_ids,
	.of_to_plat	= mmio_efuse_of_to_plat,
	.plat_auto	= sizeof(struct mmio_efuse_plat),
	.ops		= &mmio_efuse_ops,
	.flags		= DM_FLAG_PRE_RELOC,
};
