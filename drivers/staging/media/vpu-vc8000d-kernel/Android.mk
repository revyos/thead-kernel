##
 # Copyright (C) 2021 Alibaba Group Holding Limited
##

LOCAL_PATH := $(call my-dir)

include $(LOCAL_PATH)/Android.mk.def

VC8000D_MAKEFILES := \
	$(LOCAL_PATH)/linux/subsys_driver/Android.mk \
	$(LOCAL_PATH)/linux/memalloc/Android.mk

include $(VC8000D_MAKEFILES)

