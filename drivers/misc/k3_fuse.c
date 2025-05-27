// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2025 Texas Instruments Incorporated, <www.ti.com>
 */

#include <errno.h>
#include <stdio.h>
#include <fuse.h>
#include <linux/arm-smccc.h>
#include <linux/bitops.h>
#include <string.h>

#define K3_SIP_OTP_WRITEBUFF 0xC2000000
#define K3_SIP_OTP_WRITE 0xC2000001
#define K3_SIP_OTP_READ 0xC2000002

int fuse_read(u32 bank, u32 word, u32 *val)
{
	struct arm_smccc_res res;

	if (bank != 0U) {
		printf("Invalid bank argument, ONLY bank 0 is supported\n");
		return -EINVAL;
	}

	/* Make SiP SMC call and send the word in the parameter register */
	arm_smccc_smc(K3_SIP_OTP_READ, bank, word,
		      0, 0, 0, 0, 0, &res);

	*val = res.a1;
	if (res.a0 != 0)
		printf("SMC call failed: Error code %ld\n", res.a0);

	return res.a0;
}

int fuse_sense(u32 bank, u32 word, u32 *val)
{
	return fuse_read(bank, word, val);
}

int fuse_prog(u32 bank, u32 word, u32 val)
{
	struct arm_smccc_res res;

	/* Bank 0 is user OTP, Bank 0xFF is bootmode OTP */
	if (bank && bank != 0xff) {
		printf("Invalid bank argument\n");
		return -EINVAL;
	}

	/* Make SiP SMC call and send the word, val and mask in the parameter register */
	arm_smccc_smc(K3_SIP_OTP_WRITE, bank, word,
		      val, GENMASK(25, 0), 0, 0, 0, &res);

	if (res.a0 != 0) {
		printf("SMC call failed: Error code %ld\n", res.a0);
		return res.a0;
	}

	if (res.a1 != val) {
		printf("Readback failed, written 0x%x readback 0x%lx\n", val, res.a1);
		return -EINVAL;
	}

	return res.a0;
}

int fuse_override(u32 bank, u32 word, u32 val)
{
	return -EPERM;
}

int fuse_writebuff(ulong addr)
{
	struct arm_smccc_res res;

	/* Make SiP SMC call and send the addr in the parameter register */
	arm_smccc_smc(K3_SIP_OTP_WRITEBUFF, (unsigned long)addr,
		      0, 0, 0, 0, 0, 0, &res);

	if (res.a0 != 0)
		printf("SMC call failed: Error code %ld\n", res.a0);

	return res.a0;
}
