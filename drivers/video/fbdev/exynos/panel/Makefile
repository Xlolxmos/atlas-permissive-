#
# Copyright (c) 2013 Samsung Electronics Co., Ltd.
#		http://www.samsung.com
#
# Licensed under GPLv2
#

obj-$(CONFIG_EXYNOS_MIPI_DSIM) += timenval.o panel.o panel_bl.o dimming.o panel_drv.o panel_irc.o decon_board.o decon_notify.o decon_void_regulator.o decon_assist.o
obj-$(and $(CONFIG_SMCDSD_LCD_DEBUG),$(subst undefined,y,$(origin CONFIG_SAMSUNG_PRODUCT_SHIP))) += dd_lcd.o dd_backlight.o dd_param.o dd_assist.o
obj-$(CONFIG_EXYNOS_DECON_MDNIE) += mdnie.o
obj-$(CONFIG_EXYNOS_DECON_LCD_SYSFS) += sysfs.o
obj-$(CONFIG_EXYNOS_DECON_LCD_SPI) += spi.o
obj-$(CONFIG_EXYNOS_DECON_LCD_COPR) += copr.o
obj-$(CONFIG_DISPLAY_USE_INFO) += dpui.o
obj-$(CONFIG_PANEL_NOTIFY) += panel_notify.o
obj-$(CONFIG_SUPPORT_DDI_FLASH) += panel_poc.o
obj-$(CONFIG_EXTEND_LIVE_CLOCK) += aod/aod_drv.o
obj-$(CONFIG_SUPPORT_POC_SPI) += panel_spi.o
obj-$(CONFIG_SUPPORT_I2C) += panel_i2c.o
obj-$(CONFIG_DYNAMIC_FREQ) += df/dynamic_freq.o


obj-$(CONFIG_EXYNOS_DECON_LCD_DUMMY_LCD_V320) += dummy_lcd/dummy_lcd.o
obj-$(CONFIG_EXYNOS_DECON_LCD_FT8203_BOE_T260) += ft8203_boe/ft8203_boe.o
obj-$(CONFIG_EXYNOS_DECON_LCD_HX83102_T270) += hx83102/hx83102.o
obj-$(CONFIG_EXYNOS_DECON_LCD_HX83112F_BOE_A13) += hx83112f_boe_a13/hx83112f_boe.o
obj-$(CONFIG_EXYNOS_DECON_LCD_NT36672C_CSOT_A13) += nt36672c_csot/nt36672c_csot.o
obj-$(CONFIG_EXYNOS_DECON_LCD_NT36672C_TIANMA_A13) += nt36672c_tianma/nt36672c_tianma.o
obj-$(CONFIG_EXYNOS_DECON_LCD_TD4375_BOE_A13) += td4375_boe/td4375_boe.o
obj-$(CONFIG_EXYNOS_DECON_LCD_ILI7807_BOE_M13) += ili7807_boe/ili7807_boe.o
obj-$(CONFIG_EXYNOS_DECON_LCD_HX83112F_BOE_M13) += hx83112f_boe_m13/hx83112f_boe.o
obj-$(CONFIG_EXYNOS_DECON_LCD_FT8720_DTC_M13) += ft8720_dtc/ft8720_dtc.o
obj-$(CONFIG_EXYNOS_DECON_LCD_TD4160_BOE_A04S) += td4160_boe/td4160_boe.o
obj-$(CONFIG_EXYNOS_DECON_LCD_NT36525C_SHARP_A04S) += nt36525c_sharp/nt36525c_sharp.o
obj-$(CONFIG_EXYNOS_DECON_LCD_NT36525C_SHARP_VE_A04S) += nt36525c_sharp_ve/nt36525c_sharp_ve.o
obj-$(CONFIG_EXYNOS_DECON_LCD_TD4162_BOE_A04S) += td4162_boe/td4162_boe.o
obj-$(CONFIG_EXYNOS_DECON_LCD_NT36525C_TIANMA_A04S) += nt36525c_tianma/nt36525c_tianma.o
obj-$(CONFIG_EXYNOS_DECON_LCD_NT36525C_DTC_A04S) += nt36525c_dtc/nt36525c_dtc.o
obj-$(CONFIG_EXYNOS_DECON_LCD_NT36672C_TIANMA_A14) += nt36672c_tianma_a14/nt36672c_tianma.o

obj-$(CONFIG_EXYNOS_DECON_LCD_EA8076_A21S) += ea8076/ea8076.o ea8076/ea8076_aod.o
obj-$(CONFIG_EXYNOS_DECON_LCD_TD4150_A21S) += td4150/td4150.o
obj-$(CONFIG_EXYNOS_DECON_LCD_HX83104_A21S) += hx83104/hx83104.o
obj-$(CONFIG_EXYNOS_DECON_LCD_NT36575_A21S) += nt36575/nt36575.o


