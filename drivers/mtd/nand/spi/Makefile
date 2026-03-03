# SPDX-License-Identifier: GPL-2.0

spinand-objs := core.o otp.o
spinand-objs += $(if $(CONFIG_MTD_SPI_NAND_ALLIANCEMEMORY),alliancememory.o,)
spinand-objs += $(if $(CONFIG_MTD_SPI_NAND_ATO),ato.o,)
spinand-objs += $(if $(CONFIG_MTD_SPI_NAND_ESMT),esmt.o,)
spinand-objs += $(if $(CONFIG_MTD_SPI_NAND_FMSH),fmsh.o,)
spinand-objs += $(if $(CONFIG_MTD_SPI_NAND_FORESEE),foresee.o,)
spinand-objs += $(if $(CONFIG_MTD_SPI_NAND_GIGADEVICE),gigadevice.o,)
spinand-objs += $(if $(CONFIG_MTD_SPI_NAND_MACRONIX),macronix.o,)
spinand-objs += $(if $(CONFIG_MTD_SPI_NAND_MICRON),micron.o,)
spinand-objs += $(if $(CONFIG_MTD_SPI_NAND_PARAGON),paragon.o,)
spinand-objs += $(if $(CONFIG_MTD_SPI_NAND_SKYHIGH),skyhigh.o,)
spinand-objs += $(if $(CONFIG_MTD_SPI_NAND_TOSHIBA),toshiba.o,)
spinand-objs += $(if $(CONFIG_MTD_SPI_NAND_WINBOND),winbond.o,)
spinand-objs += $(if $(CONFIG_MTD_SPI_NAND_XTX),xtx.o,)
obj-$(CONFIG_MTD_SPI_NAND) += spinand.o
