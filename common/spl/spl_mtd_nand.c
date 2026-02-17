// SPDX-License-Identifier: GPL-2.0+
/*
 * FIT image loader using MTD read
 *
 * Copyright (C) 2026 Texas Instruments Incorporated - http://www.ti.com/
 *	Anurag Dutta <a-dutta@ti.com>
 */
#include <config.h>
#include <image.h>
#include <log.h>
#include <spl.h>
#include <asm/io.h>
#include <mtd.h>

static int spl_mtd_load_image(struct spl_image_info *spl_image,
			      struct spl_boot_device *bootdev)
{
	struct mtd_info *mtd;
	int ret;

	mtd = spl_prepare_mtd(BOOT_DEVICE_SPINAND);
	if (IS_ERR_OR_NULL(mtd)) {
		printf("MTD device %s not found, ret %ld\n", "spi-nand",
		       PTR_ERR(mtd));
		return -EINVAL;
	}

	return spl_mtd_load(spl_image, mtd, bootdev);
}

SPL_LOAD_IMAGE_METHOD("SPINAND", 0, BOOT_DEVICE_SPINAND, spl_mtd_load_image);
