# Copyright (C) 2008-2015 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	Log.cpp \
	SharedBuffer.cpp \
	Static.cpp \
	String8.cpp \
	String16.cpp \
	Timers.cpp \
	Unicode.cpp

ifeq ($(TARGET_ARCH),mips)
LOCAL_CFLAGS += -DALIGN_DOUBLE
endif
LOCAL_CFLAGS += -Werror
LOCAL_CFLAGS += -DOS_PATH_SEPARATOR=\'/\'

LOCAL_C_INCLUDES += \
	include

LOCAL_MODULE := libutils

include $(BUILD_STATIC_LIBRARY)
