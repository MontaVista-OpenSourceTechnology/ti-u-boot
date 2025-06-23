// SPDX-License-Identifier:     GPL-2.0+
/*
 * K3: ARM64 MMU setup
 *
 * Copyright (C) 2018-2020 Texas Instruments Incorporated - https://www.ti.com/
 *	Lokesh Vutla <lokeshvutla@ti.com>
 *	Suman Anna <s-anna@ti.com>
 * (This file is derived from arch/arm/mach-zynqmp/cpu.c)
 *
 */

#include <asm/system.h>
#include <asm/armv8/mmu.h>

/* We need extra 5 entries for:
 * SoC peripherals, flash, atf-carveout, tee-carveout and the sentinel value.
 */
#define K3_MMU_REGIONS_COUNT ((CONFIG_NR_DRAM_BANKS) + 5)

struct mm_region k3_mem_map[K3_MMU_REGIONS_COUNT] = {
	{ /* SoC Peripherals */
		.virt = 0x0UL,
		.phys = 0x0UL,
		.size = 0x80000000UL,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
			 PTE_BLOCK_NON_SHARE |
			 PTE_BLOCK_PXN | PTE_BLOCK_UXN
	}, { /* Flash peripherals */
		.virt = 0x500000000UL,
		.phys = 0x500000000UL,
		.size = 0x380000000UL,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
			 PTE_BLOCK_NON_SHARE |
			 PTE_BLOCK_PXN | PTE_BLOCK_UXN
	}, { /* Map SPL load region and the next 128MiB as cacheable */
		.virt = CONFIG_SPL_TEXT_BASE,
		.phys = CONFIG_SPL_TEXT_BASE,
		.size = SZ_128M,
		.attrs = PTE_BLOCK_MEMTYPE(MT_NORMAL) |
			 PTE_BLOCK_INNER_SHARE
	}, { /* List terminator */
		0,
	}
};

struct mm_region *mem_map = k3_mem_map;

	}


