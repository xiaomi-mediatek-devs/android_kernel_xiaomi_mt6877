#
# Copyright (C) 2018 MediaTek Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#

ifeq ($(CONFIG_MTK_CPU_CTRL),y)
obj-y += cpu_ctrl.o
endif

ifeq ($(CONFIG_MACH_MT6893),y)
ccflags-y += \
		-DMTK_K14_CPU_BOOST
else ifeq ($(CONFIG_MACH_MT6885),y)
ccflags-y += \
		-DMTK_K14_CPU_BOOST
else ifeq ($(CONFIG_MACH_MT6877),y)
ccflags-y += \
		-DMTK_K14_CPU_BOOST
else ifeq ($(CONFIG_MACH_MT6873),y)
ccflags-y += \
		-DMTK_K14_CPU_BOOST
else ifeq ($(CONFIG_MACH_MT6853),y)
ccflags-y += \
		-DMTK_K14_CPU_BOOST
else ifeq ($(CONFIG_MACH_MT6781),y)
ccflags-y += \
		-DMTK_K14_CPU_BOOST
else ifeq ($(CONFIG_MACH_MT6739),y)
ccflags-y += \
		-DMTK_K14_CPU_BOOST
else ifeq ($(CONFIG_MACH_MT6833),y)
ccflags-y += \
		-DMTK_K14_CPU_BOOST
else ifeq ($(CONFIG_MACH_MT6768),y)
ccflags-y += \
		-DMTK_K14_CPU_BOOST
else ifeq ($(CONFIG_MACH_MT6771),y)
ccflags-y += \
		-DMTK_K14_CPU_BOOST
else ifeq ($(CONFIG_MACH_MT6785),y)
ccflags-y += \
		-DMTK_K14_CPU_BOOST
endif

obj-$(CONFIG_MTK_CPU_CTRL_CFP) += cpu_ctrl_cfp.o
ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/include/
ccflags-y += -I$(srctree)drivers/misc/mediatek/performance/include/
