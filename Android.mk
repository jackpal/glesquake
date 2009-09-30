#########################################################################
# Quake game
# This makefile builds both an activity and a shared library.
#########################################################################
ifneq ($(TARGET_SIMULATOR),true) # not 64 bit clean

TOP_LOCAL_PATH:= $(call my-dir)

# Build Quake activity

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := user

LOCAL_SRC_FILES := $(call all-subdir-java-files)

LOCAL_PACKAGE_NAME := Quake

LOCAL_JNI_SHARED_LIBRARIES := libquake

include $(BUILD_PACKAGE)

#########################################################################
# Build Quake Shared Library
#########################################################################

LOCAL_PATH:= $(LOCAL_PATH)/quake/src/WinQuake

include $(CLEAR_VARS)

# Optional tag would mean it doesn't get installed by default
LOCAL_MODULE_TAGS := optional

LOCAL_CFLAGS := -Werror

LOCAL_SRC_FILES:= \
  cd_null.cpp \
  cl_demo.cpp \
  cl_input.cpp \
  cl_main.cpp \
  cl_parse.cpp \
  cl_tent.cpp \
  chase.cpp \
  cmd.cpp \
  common.cpp \
  console.cpp \
  crc.cpp \
  cvar.cpp \
  gl_draw.cpp \
  gl_mesh.cpp \
  gl_model.cpp \
  gl_refrag.cpp \
  gl_rlight.cpp \
  gl_rmain.cpp \
  gl_rmisc.cpp \
  gl_rsurf.cpp \
  gl_screen.cpp \
  gl_vidandroid.cpp \
  gl_warp.cpp \
  host.cpp \
  host_cmd.cpp \
  keys.cpp \
  main.cpp \
  masterMain.cpp \
  mathlib.cpp \
  menu.cpp \
  net_bsd.cpp \
  net_dgrm.cpp \
  net_loop.cpp \
  net_main.cpp \
  net_vcr.cpp \
  net_udp.cpp \
  nonintel.cpp \
  pr_cmds.cpp \
  pr_edict.cpp \
  pr_exec.cpp \
  r_part.cpp \
  sbar.cpp \
  snd_dma.cpp \
  snd_mem.cpp \
  snd_mix.cpp \
  snd_android.cpp \
  sv_main.cpp \
  sv_phys.cpp \
  sv_move.cpp \
  sv_user.cpp \
  sys_android.cpp \
  view.cpp \
  wad.cpp \
  world.cpp \
  zone.cpp

LOCAL_SHARED_LIBRARIES := \
	libutils \
	libmedia \
	libEGL \
	libGLESv1_CM

LOCAL_MODULE := libquake

LOCAL_ARM_MODE := arm

LOCAL_PRELINK_MODULE := false

include $(BUILD_SHARED_LIBRARY)

#########################################################################
# Build stand-alone quake executable on device
#########################################################################

ifneq ($(BUILD_TINY_ANDROID),true)
ifeq ($(TARGET_ARCH),arm)

LOCAL_PATH:= $(TOP_LOCAL_PATH)/standalone
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= main.cpp

LOCAL_SHARED_LIBRARIES := libc libm libui libquake libEGL libGLESv1_CM

LOCAL_MODULE:= quake

LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)

endif
endif

endif # TARGET_SIMULATOR
