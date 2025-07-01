// SPDX-License-Identifier: GPL-2.0+
/*
 * K3: Common Architecture initialization
 *
 * Copyright (C) 2018 Texas Instruments Incorporated - https://www.ti.com/
 *	Lokesh Vutla <lokeshvutla@ti.com>
 */

#include <config.h>
#include <cpu_func.h>
#include <image.h>
#include <init.h>
#include <log.h>
#include <spl.h>
#include <asm/global_data.h>
#include <linux/printk.h>
#include "common.h"
#include <dm.h>
#include <dm/of_access.h>
#include <dm/ofnode.h>
#include <remoteproc.h>
#include <asm/cache.h>
#include <linux/soc/ti/ti_sci_protocol.h>
#include <fdt_support.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <fs_loader.h>
#include <fs.h>
#include <efi_loader.h>
#include <env.h>
#include <elf.h>
#include <soc.h>
#include <dm/uclass-internal.h>
#include <dm/device-internal.h>
#include <wait_bit.h>

#define CLKSTOP_TRANSITION_TIMEOUT_MS	10
#define K3_R5_MEMREGION_LPM_METADATA_OFFSET	0x108000

#define PROC_BOOT_CTRL_FLAG_R5_CORE_HALT	0x00000001
#define PROC_ID_MCU_R5FSS0_CORE1		0x02
#define PROC_BOOT_CFG_FLAG_R5_LOCKSTEP		0x00000100

#include <asm/arch/k3-qos.h>
#include <mach/k3-ddr.h>

struct ti_sci_handle *get_ti_sci_handle(void)
{
	struct udevice *dev;
	int ret;

	ret = uclass_get_device_by_driver(UCLASS_FIRMWARE,
					  DM_DRIVER_GET(ti_sci), &dev);
	if (ret)
		panic("Failed to get SYSFW (%d)\n", ret);

	return (struct ti_sci_handle *)ti_sci_get_handle_from_sysfw(dev);
}

void k3_sysfw_print_ver(void)
{
	struct ti_sci_handle *ti_sci = get_ti_sci_handle();
	char fw_desc[sizeof(ti_sci->version.firmware_description) + 1];

	/*
	 * Output System Firmware version info. Note that since the
	 * 'firmware_description' field is not guaranteed to be zero-
	 * terminated we manually add a \0 terminator if needed. Further
	 * note that we intentionally no longer rely on the extended
	 * printf() formatter '%.*s' to not having to require a more
	 * full-featured printf() implementation.
	 */
	strncpy(fw_desc, ti_sci->version.firmware_description,
		sizeof(ti_sci->version.firmware_description));
	fw_desc[sizeof(fw_desc) - 1] = '\0';

	printf("SYSFW ABI: %d.%d (firmware rev 0x%04x '%s')\n",
	       ti_sci->version.abi_major, ti_sci->version.abi_minor,
	       ti_sci->version.firmware_revision, fw_desc);
}

void __maybe_unused k3_dm_print_ver(void)
{
	struct ti_sci_handle *ti_sci = get_ti_sci_handle();
	struct ti_sci_firmware_ops *fw_ops = &ti_sci->ops.fw_ops;
	struct ti_sci_dm_version_info dm_info = {0};
	u64 fw_caps;
	int ret;

	ret = fw_ops->query_dm_cap(ti_sci, &fw_caps);
	if (ret) {
		printf("Failed to query DM firmware capability %d\n", ret);
		return;
	}

	if (!(fw_caps & TI_SCI_MSG_FLAG_FW_CAP_DM))
		return;

	ret = fw_ops->get_dm_version(ti_sci, &dm_info);
	if (ret) {
		printf("Failed to fetch DM firmware version %d\n", ret);
		return;
	}

	printf("DM ABI: %d.%d (firmware ver 0x%04x '%s--%s' "
	       "patch_ver: %d)\n", dm_info.abi_major, dm_info.abi_minor,
	       dm_info.dm_ver, dm_info.sci_server_version,
	       dm_info.rm_pm_hal_version, dm_info.patch_ver);
}

void mmr_unlock(uintptr_t base, u32 partition)
{
	/* Translate the base address */
	uintptr_t part_base = base + partition * CTRL_MMR0_PARTITION_SIZE;

	/* Unlock the requested partition if locked using two-step sequence */
	writel(CTRLMMR_LOCK_KICK0_UNLOCK_VAL, part_base + CTRLMMR_LOCK_KICK0);
	writel(CTRLMMR_LOCK_KICK1_UNLOCK_VAL, part_base + CTRLMMR_LOCK_KICK1);
}

static void wkup_ctrl_remove_can_io_isolation(void)
{
	const void *wait_reg = (const void *)(WKUP_CTRL_MMR0_BASE +
					      WKUP_CTRL_MMR_CANUART_WAKE_STAT1);
	int ret;
	u32 reg = 0;

	/* Program magic word */
	reg = readl(WKUP_CTRL_MMR0_BASE + WKUP_CTRL_MMR_CANUART_WAKE_CTRL);
	reg |= WKUP_CTRL_MMR_CANUART_WAKE_CTRL_MW << WKUP_CTRL_MMR_CANUART_WAKE_CTRL_MW_SHIFT;
	writel(reg, WKUP_CTRL_MMR0_BASE + WKUP_CTRL_MMR_CANUART_WAKE_CTRL);

	/* Set enable bit. */
	reg |= WKUP_CTRL_MMR_CANUART_WAKE_CTRL_MW_LOAD_EN;
	writel(reg, WKUP_CTRL_MMR0_BASE + WKUP_CTRL_MMR_CANUART_WAKE_CTRL);

       /* Clear enable bit. */
	reg &= ~WKUP_CTRL_MMR_CANUART_WAKE_CTRL_MW_LOAD_EN;
	writel(reg, WKUP_CTRL_MMR0_BASE + WKUP_CTRL_MMR_CANUART_WAKE_CTRL);

	/* wait for CAN_ONLY_IO signal to be 0 */
	ret = wait_for_bit_32(wait_reg,
			      WKUP_CTRL_MMR_CANUART_WAKE_STAT1_CANUART_IO_MODE,
			      false,
			      CLKSTOP_TRANSITION_TIMEOUT_MS,
			      false);
	if (ret < 0)
		return;

	/* Reset magic word */
	writel(0, WKUP_CTRL_MMR0_BASE + WKUP_CTRL_MMR_CANUART_WAKE_CTRL);

	/* Remove WKUP IO isolation */
	reg = readl(WKUP_CTRL_MMR0_BASE + WKUP_CTRL_MMR_PMCTRL_IO_0);
	reg = reg & WKUP_CTRL_MMR_PMCTRL_IO_0_WRITE_MASK & ~WKUP_CTRL_MMR_PMCTRL_IO_0_GLOBAL_WUEN_0;
	writel(reg, WKUP_CTRL_MMR0_BASE + WKUP_CTRL_MMR_PMCTRL_IO_0);

	/* clear global IO isolation */
	reg = readl(WKUP_CTRL_MMR0_BASE + WKUP_CTRL_MMR_PMCTRL_IO_0);
	reg = reg & WKUP_CTRL_MMR_PMCTRL_IO_0_WRITE_MASK & ~WKUP_CTRL_MMR_PMCTRL_IO_0_IO_ISO_CTRL_0;
	writel(reg, WKUP_CTRL_MMR0_BASE + WKUP_CTRL_MMR_PMCTRL_IO_0);

	writel(0, WKUP_CTRL_MMR0_BASE + WKUP_CTRL_MMR_DEEPSLEEP_CTRL);
	writel(0, WKUP_CTRL_MMR0_BASE + WKUP_CTRL_MMR_PMCTRL_IO_GLB);
}

static bool wkup_ctrl_canuart_wakeup_active(void)
{
	return !!(readl(WKUP_CTRL_MMR0_BASE + WKUP_CTRL_MMR_CANUART_WAKE_STAT1) &
		WKUP_CTRL_MMR_CANUART_WAKE_STAT1_CANUART_IO_MODE);
}

static bool wkup_ctrl_canuart_magic_word_set(void)
{
	return readl(WKUP_CTRL_MMR0_BASE + WKUP_CTRL_MMR_CANUART_WAKE_OFF_MODE_STAT) ==
		WKUP_CTRL_MMR_CANUART_WAKE_OFF_MODE_STAT_MW;
}

void wkup_ctrl_remove_can_io_isolation_if_set(void)
{
	if (wkup_ctrl_canuart_wakeup_active() && !wkup_ctrl_canuart_magic_word_set())
		wkup_ctrl_remove_can_io_isolation();
}

bool wkup_ctrl_is_lpm_exit(void)
{
	return IS_ENABLED(CONFIG_K3_IODDR) &&
		wkup_ctrl_canuart_wakeup_active() &&
		wkup_ctrl_canuart_magic_word_set();
}

#if IS_ENABLED(CONFIG_K3_IODDR)
int wkup_r5f_am62_lpm_meta_data_addr(u32 *meta_data_addr)
{
	struct ofnode_phandle_args memregion_phandle;
	ofnode memregion;
	ofnode wkup_bus;
	int ret;
	u64 dm_resv_addr;

	wkup_bus = ofnode_path("/bus@f0000/bus@b00000");
	if (!ofnode_valid(wkup_bus)) {
		printf("Failed to find wkup bus\n");
		return -EINVAL;
	}

	memregion = ofnode_by_compatible(wkup_bus, "ti,am62-r5f");
	if (!ofnode_valid(memregion)) {
		printf("Failed to find r5f devicetree node ti,am62-r5f\n");
		return -EINVAL;
	}

	ret = ofnode_parse_phandle_with_args(memregion, "memory-region", NULL,
			0, 1, &memregion_phandle);
	if (ret) {
		printf("Failed to parse phandle for second memory region\n");
		return ret;
	}

	ret = ofnode_read_u64_index(memregion_phandle.node, "reg", 0, &dm_resv_addr);
	if (ret) {
		printf("Failed to read memory region offset\n");
		return ret;
	}

	if (dm_resv_addr > 0xFFFFFFFFULL) {
		printf("DM reserved section address exceeds 32 bits\n");
		return -ERANGE;
	}

	*meta_data_addr = dm_resv_addr + K3_R5_MEMREGION_LPM_METADATA_OFFSET;

	return 0;
}
static int lpm_restore_context(u64 ctx_addr)
{
	struct ti_sci_handle *ti_sci = get_ti_sci_handle();
	int ret;

	ret = ti_sci->ops.lpm_ops.restore_context(ti_sci, ctx_addr);
	if (ret)
		printf("Failed to restore context from DDR %d\n", ret);

	return ret;
}

struct lpm_meta_data {
	u64 dm_jump_address;
	u64 tifs_context_save_address;
	u64 reserved[30];
} __packed__;

void __noreturn lpm_resume_from_ddr(u32 meta_data_addr)
{
	typedef void __noreturn (*image_entry_noargs_t)(void);
	image_entry_noargs_t image_entry;
	int ret;
	struct lpm_meta_data *lpm_data = (struct lpm_meta_data *)meta_data_addr;
	u64 tifs_context_save_address = lpm_data->tifs_context_save_address;

	ret = lpm_restore_context(tifs_context_save_address);
	if (ret)
		panic("Failed to restore context from 0x%lx%lx\n",
		      (unsigned long)(tifs_context_save_address >> 32U),
			  (unsigned long)tifs_context_save_address);

	if (lpm_data->dm_jump_address > 0xFFFFFFFFULL)
		panic("Failed to jump due to DM load address exceeding 32 bits\n");

	image_entry = (image_entry_noargs_t)(unsigned long)lpm_data->dm_jump_address;
	printf("Resuming from DDR, jumping to stored DM loadaddr 0x%p, TIFS context restored from 0x%lx%lx\n",
	       image_entry, (unsigned long)(tifs_context_save_address >> 32U),
		   (unsigned long)tifs_context_save_address);

	image_entry();
}
#else
int wkup_r5f_am62_lpm_meta_data_addr(u32 *meta_data_addr)
{
	return -EINVAL;
}

void lpm_resume_from_ddr(u32 meta_data_addr) {}
#endif

bool is_rom_loaded_sysfw(struct rom_extended_boot_data *data)
{
	if (strncmp(data->header, K3_ROM_BOOT_HEADER_MAGIC, 7))
		return false;

	return data->num_components > 1;
}

DECLARE_GLOBAL_DATA_PTR;

#ifdef CONFIG_K3_EARLY_CONS
int early_console_init(void)
{
	struct udevice *dev;
	int ret;

	gd->baudrate = CONFIG_BAUDRATE;

	ret = uclass_get_device_by_seq(UCLASS_SERIAL, CONFIG_K3_EARLY_CONS_IDX,
				       &dev);
	if (ret) {
		printf("Error getting serial dev for early console! (%d)\n",
		       ret);
		return ret;
	}

	gd->cur_serial_dev = dev;
	gd->flags |= GD_FLG_SERIAL_READY;
	gd->flags |= GD_FLG_HAVE_CONSOLE;

	return 0;
}
#endif

#if CONFIG_IS_ENABLED(FIT_IMAGE_POST_PROCESS) && !IS_ENABLED(CONFIG_SYS_K3_SPL_ATF)
void board_fit_image_post_process(const void *fit, int node, void **p_image,
				  size_t *p_size)
{
	ti_secure_image_check_binary(p_image, p_size);
	ti_secure_image_post_process(p_image, p_size);
}
#endif

#ifndef CONFIG_SYSRESET
void reset_cpu(void)
{
}
#endif

enum k3_device_type get_device_type(void)
{
	u32 sys_status = readl(K3_SEC_MGR_SYS_STATUS);

	u32 sys_dev_type = (sys_status & SYS_STATUS_DEV_TYPE_MASK) >>
			SYS_STATUS_DEV_TYPE_SHIFT;

	u32 sys_sub_type = (sys_status & SYS_STATUS_SUB_TYPE_MASK) >>
			SYS_STATUS_SUB_TYPE_SHIFT;

	switch (sys_dev_type) {
	case SYS_STATUS_DEV_TYPE_GP:
		return K3_DEVICE_TYPE_GP;
	case SYS_STATUS_DEV_TYPE_TEST:
		return K3_DEVICE_TYPE_TEST;
	case SYS_STATUS_DEV_TYPE_EMU:
		return K3_DEVICE_TYPE_EMU;
	case SYS_STATUS_DEV_TYPE_HS:
		if (sys_sub_type == SYS_STATUS_SUB_TYPE_VAL_FS)
			return K3_DEVICE_TYPE_HS_FS;
		else
			return K3_DEVICE_TYPE_HS_SE;
	default:
		return K3_DEVICE_TYPE_BAD;
	}
}

#if defined(CONFIG_DISPLAY_CPUINFO)
static const char *get_device_type_name(void)
{
	enum k3_device_type type = get_device_type();

	switch (type) {
	case K3_DEVICE_TYPE_GP:
		return "GP";
	case K3_DEVICE_TYPE_TEST:
		return "TEST";
	case K3_DEVICE_TYPE_EMU:
		return "EMU";
	case K3_DEVICE_TYPE_HS_FS:
		return "HS-FS";
	case K3_DEVICE_TYPE_HS_SE:
		return "HS-SE";
	default:
		return "BAD";
	}
}

int print_cpuinfo(void)
{
	struct udevice *soc;
	char name[64];
	int ret;

	printf("SoC:   ");

	ret = soc_get(&soc);
	if (ret) {
		printf("UNKNOWN\n");
		return 0;
	}

	ret = soc_get_family(soc, name, 64);
	if (!ret) {
		printf("%s ", name);
	}

	ret = soc_get_revision(soc, name, 64);
	if (!ret) {
		printf("%s ", name);
	}

	printf("%s\n", get_device_type_name());

	return 0;
}
#endif

#ifdef CONFIG_ARM64
void board_prep_linux(struct bootm_headers *images)
{
	debug("Linux kernel Image start = 0x%lx end = 0x%lx\n",
	      images->os.start, images->os.end);
	__asm_flush_dcache_range(images->os.start,
				 ROUND(images->os.end,
				       CONFIG_SYS_CACHELINE_SIZE));
}
#endif

void spl_enable_cache(void)
{
#if !(defined(CONFIG_SYS_ICACHE_OFF) && defined(CONFIG_SYS_DCACHE_OFF))
	gd->ram_top = CFG_SYS_SDRAM_BASE;
	int ret = 0;

	dram_init();

	gd->ram_top += get_effective_memsize();
	/* keep ram_top in the 32-bit address space */
	if (gd->ram_top >= 0x100000000)
		gd->ram_top = (phys_addr_t)0x100000000;

	gd->relocaddr = gd->ram_top;

	ret = spl_reserve_video_from_ram_top();
	if (ret)
		panic("Failed to reserve framebuffer memory (%d)\n", ret);

	if (IS_ENABLED(CONFIG_ARM64)) {
		ret = k3_mem_map_init();
		if (ret)
			panic("Failed to setup MMU table (%d)\n", ret);
	}

	/* reserve TLB table */
	gd->arch.tlb_size = PGTABLE_SIZE;

	gd->arch.tlb_addr = gd->relocaddr - gd->arch.tlb_size;
	gd->arch.tlb_addr &= ~(0x10000 - 1);
	debug("TLB table from %08lx to %08lx\n", gd->arch.tlb_addr,
	      gd->arch.tlb_addr + gd->arch.tlb_size);
	gd->relocaddr = gd->arch.tlb_addr;

	enable_caches();
#endif
}

static __maybe_unused void k3_dma_remove(void)
{
	struct udevice *dev;
	int rc;

	rc = uclass_find_device(UCLASS_DMA, 0, &dev);
	if (!rc && dev) {
		rc = device_remove(dev, DM_REMOVE_NORMAL);
		if (rc)
			pr_warn("Cannot remove dma device '%s' (err=%d)\n",
				dev->name, rc);
	} else
		pr_warn("DMA Device not found (err=%d)\n", rc);
}

#if IS_ENABLED(CONFIG_SPL_OS_BOOT) && !IS_ENABLED(CONFIG_ARM64)
static bool tifalcon_loaded;

int spl_start_uboot(void)
{
	if (!tifalcon_loaded)
		return 1;
	return 0;
}

static int k3_falcon_fdt_fixup(void *fdt)
{
	struct disk_partition info;
	struct blk_desc *dev_desc;
	char bootmedia[32];
	char bootpart[32];
	char str[256];
	int ret;

	strlcpy(bootmedia, env_get("boot"), sizeof(bootmedia));
	strlcpy(bootpart, env_get("bootpart"), sizeof(bootpart));
	ret = blk_get_device_part_str(bootmedia, bootpart, &dev_desc, &info, 0);
	if (ret < 0) {
		printf("%s: Failed to get part details for %s %s [%d]\n",
		       __func__, bootmedia, bootpart, ret);
		return ret;
	}
	snprintf(str, sizeof(str), "console=%s root=PARTUUID=%s rootwait",
		 env_get("console"), info.uuid);

	fdt_set_totalsize(fdt, fdt_totalsize(fdt) + CONFIG_SYS_FDT_PAD);
	ret = fdt_find_and_setprop(fdt, "/chosen", "bootargs", str,
				   strlen(str) + 1, 1);
	if (ret) {
		printf("%s: Could not set bootargs: %s\n", __func__,
		       fdt_strerror(ret));
		return ret;
	}
	debug("Set bootargs to: %s\n", str);

	if (IS_ENABLED(CONFIG_OF_BOARD_SETUP)) {
		ret = ft_board_setup(fdt, gd->bd);
		if (ret) {
			printf("%s: Failed in board fdt fixups: %s\n", __func__,
			       fdt_strerror(ret));
			return ret;
		}
	}

	if (IS_ENABLED(CONFIG_OF_SYSTEM_SETUP)) {
		ret = ft_system_setup(fdt, gd->bd);
		if (ret) {
			printf("%s: Failed in system fdt fixups: %s\n",
			       __func__, fdt_strerror(ret));
			return ret;
		}
	}

	return 0;
}

static int k3_falcon_prep(void)
{
	struct spl_image_loader *loader, *drv;
	struct spl_image_info kernel_image;
	struct spl_boot_device bootdev;
	int ret = -ENXIO, n_ents;
	void *fdt;

	tifalcon_loaded = true;
	memset(&kernel_image, '\0', sizeof(kernel_image));
	drv = ll_entry_start(struct spl_image_loader, spl_image_loader);
	n_ents = ll_entry_count(struct spl_image_loader, spl_image_loader);
	bootdev.boot_device = spl_boot_device();

	/* Load payload from MMC instead of SPI due to it's limited size */
	if (bootdev.boot_device == BOOT_DEVICE_SPI) {
		if (strncmp(env_get("mmcdev"), "0", sizeof("0")) == 0)
			bootdev.boot_device = BOOT_DEVICE_MMC1;
		else
			bootdev.boot_device = BOOT_DEVICE_MMC2;
	}

	for (loader = drv; loader != drv + n_ents; loader++) {
		if (bootdev.boot_device != loader->boot_device)
			continue;
		if (loader) {
			printf("Loading falcon payload from %s\n",
			       spl_loader_name(loader));
			ret = loader->load_image(&kernel_image, &bootdev);
			if (ret)
				continue;

			fdt = spl_image_fdt_addr(&kernel_image);
			if (!fdt)
				fdt = (void *)CONFIG_SPL_PAYLOAD_ARGS_ADDR;
			ret = k3_falcon_fdt_fixup(fdt);
			if (ret) {
				printf("%s: Failed performing fdt fixups in falcon flow: [%d]\n",
				       __func__, ret);
				return ret;
			}
			return 0;
		}
	}

	return ret;
}
#endif /* falcon boot on r5 spl */

void spl_board_prepare_for_boot(void)
{
#if IS_ENABLED(CONFIG_SPL_OS_BOOT) && !IS_ENABLED(CONFIG_ARM64)
	int ret;

	ret = k3_falcon_prep();
	if (ret)
		panic("%s: Failed to boot in falcon mode: %d\n", __func__, ret);
#endif
#if !(defined(CONFIG_SYS_ICACHE_OFF) && defined(CONFIG_SYS_DCACHE_OFF))
	dcache_disable();
#endif
#if IS_ENABLED(CONFIG_SPL_DMA) && IS_ENABLED(CONFIG_SPL_DM_DEVICE_REMOVE)
	k3_dma_remove();
#endif
}

#if !(defined(CONFIG_SYS_ICACHE_OFF) && defined(CONFIG_SYS_DCACHE_OFF))
void spl_board_prepare_for_linux(void)
{
	dcache_disable();
}
#endif

int misc_init_r(void)
{
	if (IS_ENABLED(CONFIG_TI_AM65_CPSW_NUSS)) {
		struct udevice *dev;
		int ret;

		ret = uclass_get_device_by_driver(UCLASS_MISC,
						  DM_DRIVER_GET(am65_cpsw_nuss),
						  &dev);
		if (ret)
			printf("Failed to probe am65_cpsw_nuss driver\n");
	}

	if (IS_ENABLED(CONFIG_TI_ICSSG_PRUETH)) {
		struct udevice *dev;
		int ret;

		ret = uclass_get_device_by_driver(UCLASS_MISC,
						  DM_DRIVER_GET(prueth),
						  &dev);
		if (ret)
			printf("Failed to probe prueth driver\n");
	}

	/* Default FIT boot on HS-SE devices */
	if (get_device_type() == K3_DEVICE_TYPE_HS_SE) {
		env_set("boot_fit", "1");
		env_set("secure_rprocs", "1");
	}

	return 0;
}

/**
 * do_board_detect() - Detect board description
 *
 * Function to detect board description. This is expected to be
 * overridden in the SoC family board file where desired.
 */
void __weak do_board_detect(void)
{
}

#if (IS_ENABLED(CONFIG_K3_QOS))
void setup_qos(void)
{
	u32 i;

	for (i = 0; i < qos_count; i++)
		writel(qos_data[i].val, (uintptr_t)qos_data[i].reg);
}
#endif

int shutdown_mcu_r5_core1(void)
{
	struct ti_sci_handle *ti_sci = get_ti_sci_handle();
	struct ti_sci_dev_ops *dev_ops = &ti_sci->ops.dev_ops;
	struct ti_sci_proc_ops *proc_ops = &ti_sci->ops.proc_ops;
	u32 dev_id_mcu_r5_core1 = put_core_ids[0];
	u64 boot_vector;
	u32 cfg, ctrl, sts;
	int cluster_mode_lockstep, ret;

	ret = proc_ops->proc_request(ti_sci, PROC_ID_MCU_R5FSS0_CORE1);
	if (ret) {
		printf("Unable to request processor control for core %d\n",
		       PROC_ID_MCU_R5FSS0_CORE1);
		return ret;
	}

	ret = proc_ops->get_proc_boot_status(ti_sci, PROC_ID_MCU_R5FSS0_CORE1,
					     &boot_vector, &cfg, &ctrl, &sts);
	if (ret) {
		printf("Unable to get Processor boot status for core %d\n",
		       PROC_ID_MCU_R5FSS0_CORE1);
		goto release_proc_ctrl;
	}

	/* Shutdown MCU R5F Core 1 only if the cluster is booted in SplitMode */
	cluster_mode_lockstep = !!(cfg & PROC_BOOT_CFG_FLAG_R5_LOCKSTEP);
	if (cluster_mode_lockstep) {
		ret = -EINVAL;
		goto release_proc_ctrl;
	}

	ret = proc_ops->set_proc_boot_ctrl(ti_sci, PROC_ID_MCU_R5FSS0_CORE1,
					   PROC_BOOT_CTRL_FLAG_R5_CORE_HALT, 0);
	if (ret) {
		printf("Unable to Halt core %d\n", PROC_ID_MCU_R5FSS0_CORE1);
		goto release_proc_ctrl;
	}

	ret = dev_ops->get_device(ti_sci, dev_id_mcu_r5_core1);
	if (ret) {
		printf("Unable to request for device %d\n",
		       dev_id_mcu_r5_core1);
	}

	ret = dev_ops->put_device(ti_sci, dev_id_mcu_r5_core1);
	if (ret) {
		printf("Unable to assert reset on core %d\n",
		       PROC_ID_MCU_R5FSS0_CORE1);
		return ret;
	}

release_proc_ctrl:
	proc_ops->proc_release(ti_sci, PROC_ID_MCU_R5FSS0_CORE1);
	return ret;
}
