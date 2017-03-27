ifeq ($(CONFIG_OF),y)

dtb-$(CONFIG_MACH_MSM8916_LGPS19)        += msm8916-lgps19.dtb
dtb-$(CONFIG_MACH_MSM8916_E2_GLOBAL_COM)	+= msm8916-e2.dtb
dtb-$(CONFIG_MACH_MSM8916_E2DS_GLOBAL_COM)	+= msm8916-e2ds.dtb
dtb-$(CONFIG_MACH_MSM8916_E2N_GLOBAL_COM)	+= msm8916-e2n.dtb
DTB_NAMES := $(subst $\",,$(CONFIG_BUILD_ARM_APPENDED_DTB_IMAGE_NAMES))
ifneq ($(DTB_NAMES),)
DTB_LIST := $(addsuffix .dtb,$(DTB_NAMES))
else
DTB_LIST := $(dtb-y)
endif

targets += dtbs
endif

DTS_TARGET := $(patsubst "%",%,$(call CONFIG_DTS_TARGET,$(1)))

$(obj)/../%.dtb: $(src)/$(DTS_TARGET)/%.dts
	$(call if_changed_dep,dtc)

dtbs: $(addprefix $(obj)/../,$(DTB_LIST))

clean-files := *.dtb
