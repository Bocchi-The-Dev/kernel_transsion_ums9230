# SPDX-License-Identifier: GPL-2.0-only
zram-y				:=	zcomp.o zram_drv.o
zram-$(CONFIG_ZRAM_DEDUP)	+=	zram_dedup.o

obj-$(CONFIG_ZRAM)	+=	zram.o

# hybridswap/Makefile always emits hyb.o, so the directory must not be entered
# unless the option is on -- otherwise zram_drv.h's duplicate members and the
# missing mem_cgroup_hybridswap fields break the plain zram build.
obj-$(CONFIG_HYBRIDSWAP) += hybridswap/
