ifeq ($(CONFIG_OPLUS_CHG),n)

# The two "n" branches below used to do 'obj-y += v1/', which meant that
# setting CONFIG_OPLUS_CHG=n did not disable the Oplus charger at all -- it
# forced v1/ in as built-in, in every configuration where v2 was also off.
# There is no way to express "build neither" through this chain, so the v1
# and v2 directory additions are commented out and controlled from the
# else branch, where the config symbol is actually honoured.
#
# v1/ is full of Qualcomm and MediaTek charger code (oplus_sy697x_*.c,
# oplus_bq*.c, oplus_battery_mtk*.o, MSM_BOOT_MODE__RF, get_boot_mode) that
# does not compile on this arm64/Unisoc tree: 27 errors. None of it is
# reachable on UMS9230. The source is left in place.

ifeq ($(CONFIG_OPLUS_CHG_V2),n)
#obj-y				+= v1/
else ifeq ($(CONFIG_OPLUS_CHG_V2),)
#obj-y				+= v1/
else
#obj-$(CONFIG_OPLUS_CHG_V2)	+= v2/
endif

else ifeq ($(CONFIG_OPLUS_CHG),)

ifeq ($(CONFIG_OPLUS_CHG_V2),n)
#obj-y				+= v1/
else ifeq ($(CONFIG_OPLUS_CHG_V2),)
#obj-y				+= v1/
else
#obj-$(CONFIG_OPLUS_CHG_V2)	+= v2/
endif

else

obj-$(CONFIG_OPLUS_CHG)		+= v1/
obj-$(CONFIG_OPLUS_CHG_V2)	+= v2/
ifeq ($(CONFIG_OPLUS_CHG),m)
ifeq ($(CONFIG_OPLUS_CHG_V2),m)
obj-m	+= oplus_chg_symbol.o
endif
endif

endif

obj-$(CONFIG_OPLUS_CHG_TEST_KIT) += test-kit/
obj-$(CONFIG_OPLUS_DYNAMIC_CONFIG) += config/
