# SPDX-License-Identifier: GPL-2.0+

nandcore-objs := core.o bbt.o
ifeq ($(CONFIG_XPL_BUILD)$(CONFIG_TPL_BUILD),)
obj-$(CONFIG_MTD_NAND_CORE) += nandcore.o
obj-$(CONFIG_MTD_RAW_NAND) += raw/
obj-$(CONFIG_MTD_SPI_NAND) += spi/
else
obj-$(CONFIG_$(PHASE_)NAND_SUPPORT) += raw/
obj-$(CONFIG_$(PHASE_)NAND_SPI_SUPPORT) += spi/ nandcore.o
endif
