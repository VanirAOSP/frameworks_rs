
LOCAL_PATH:=$(call my-dir)

rs_base_CFLAGS := -Werror -Wall -Wno-unused-parameter -Wno-unused-variable $(call-cc-cpp-option,-Qunused-arguments)
ifeq ($(TARGET_BUILD_PDK), true)
  rs_base_CFLAGS += -D__RS_PDK__
endif

ifneq ($(OVERRIDE_RS_DRIVER),)
  rs_base_CFLAGS += -DOVERRIDE_RS_DRIVER=$(OVERRIDE_RS_DRIVER)
endif

include $(CLEAR_VARS)
LOCAL_CLANG := true
LOCAL_MODULE := libRSCpuRef

LOCAL_SRC_FILES:= \
	rsCpuCore.cpp \
	rsCpuScript.cpp \
	rsCpuRuntimeMath.cpp \
	rsCpuRuntimeStubs.cpp \
	rsCpuScriptGroup.cpp \
	rsCpuIntrinsic.cpp \
	rsCpuIntrinsic3DLUT.cpp \
	rsCpuIntrinsicBlend.cpp \
	rsCpuIntrinsicBlur.cpp \
	rsCpuIntrinsicColorMatrix.cpp \
	rsCpuIntrinsicConvolve3x3.cpp \
	rsCpuIntrinsicConvolve5x5.cpp \
	rsCpuIntrinsicHistogram.cpp \
	rsCpuIntrinsicLUT.cpp \
	rsCpuIntrinsicYuvToRGB.cpp

ifeq ($(ARCH_ARM_HAVE_NEON),true)
    LOCAL_CFLAGS += -DARCH_ARM_HAVE_NEON
endif

ifeq ($(ARCH_ARM_HAVE_VFP),true)
    LOCAL_CFLAGS += -DARCH_ARM_HAVE_VFP
    LOCAL_SRC_FILES+= \
        rsCpuIntrinsics_neon.S \
        rsCpuIntrinsics_neon_ColorMatrix.S
    LOCAL_ASFLAGS := -mfpu=neon
endif

ifeq ($(ARCH_X86_HAVE_SSE2), true)
    LOCAL_CFLAGS += -DARCH_X86_HAVE_SSE2
endif

LOCAL_SHARED_LIBRARIES += libRS libcutils libutils liblog libsync
LOCAL_SHARED_LIBRARIES += libbcc libbcinfo

LOCAL_C_INCLUDES += frameworks/compile/libbcc/include
LOCAL_C_INCLUDES += frameworks/rs

LOCAL_CFLAGS += $(rs_base_CFLAGS)

# If we have msm* set and we are a cortex-a15, lets assign the snapdragon optimized -mtune=krait2 instead of
# -mtune=cortex-a15 since this flag is not supported by clang
ifneq ($(TARGET_CLANG_VERSION),)
ifeq ($(filter-out msm%,$(TARGET_CLANG_VERSION)),)
  ifeq ($(TARGET_ARCH_VARIANT_CPU),cortex-a15)
    LOCAL_CFLAGS += -mtune=krait2 -mcpu=krait2
  endif
endif
endif

LOCAL_LDLIBS := -lpthread -ldl
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
