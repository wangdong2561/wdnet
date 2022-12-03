LOCAL_PATH :=$(call my-dir)


AUTO_SUBVERSION_FILE_NAME := $(LOCAL_PATH)/include/auto_modsubver.h

include $(LOCAL_PATH)/autosubver.mk
$(call autogenerate_modsubversion, $(AUTO_SUBVERSION_FILE_NAME))

mbnet_clean: private_local:=$(LOCAL_PATH)
mbnet_clean:
	rm -rf $(private_local)/include/auto_modsubver.h
	rm -rf $(private_local)/out
	rm -rf $(private_local)/release
$(call add-module-clean,,mbnet_clean)

include $(CLEAR_VARS)
LOCAL_MODULE := mbnetlib_so
LOCAL_MODULE_FILENAME := libmbnet
LOCAL_RELATE_MODE := plat

LOCAL_SRC_FILES := $(call get-all-wildcard-files, \
				./src/, \
				.c) 

LOCAL_EXPORT_C_INCLUDES := \
		$(LOCAL_PATH)/include/

LOCAL_C_INCLUDES := $(LOCAL_EXPORT_C_INCLUDES)


LOCAL_CFLAGS := -fPIC
LOCAL_LDLIBS := -lm -lpthread -ldl 

ifdef TARGET_RELEASE_DIR
LOCAL_RELEASE_PATH := $(TARGET_RELEASE_DIR)/cbb/mbnet/lib/$(HOST_OS)_$(TARGET_PLATFORM)
endif
LOCAL_RELEASE_PATH += $(LOCAL_PATH)/release/lib/$(HOST_OS)_$(TARGET_PLATFORM)
include $(BUILD_SHARED_LIBRARY)


include $(CLEAR_VARS)
LOCAL_MODULE := mbnetlib_a
LOCAL_MODULE_FILENAME := libmbnet
LOCAL_RELATE_MODE := plat
 
LOCAL_SRC_FILES := $(call get-all-wildcard-files, \
				./src/, \
				.c)

LOCAL_EXPORT_C_INCLUDES := \
			$(LOCAL_PATH)/include

LOCAL_C_INCLUDES := $(LOCAL_EXPORT_C_INCLUDES) 
LOCAL_EXPORT_LDLIBS := -lm -lpthread -ldl
#LOCAL_LDLIBS := -lpthread

LOCAL_CFLAGS := -fPIC

ifdef TARGET_RELEASE_DIR
LOCAL_RELEASE_PATH := $(TARGET_RELEASE_DIR)/cbb/mbnet/lib/$(HOST_OS)_$(TARGET_PLATFORM)
endif
LOCAL_RELEASE_PATH += $(LOCAL_PATH)/release/lib/$(HOST_OS)_$(TARGET_PLATFORM)
include $(BUILD_STATIC_LIBRARY)


#Release header files.
include $(CLEAR_VARS)
ifdef TARGET_RELEASE_DIR
LOCAL_MODULE := mbnet_h_install
LOCAL_DEPS_MODULES := mbnetlib_so mbnetlib_a
LOCAL_TARGET_TOP := $(LOCAL_PATH)
LOCAL_RELEASE_PATH := $(TARGET_RELEASE_DIR)/cbb/mbnet/include
LOCAL_TARGET_COPY_FILES := include/mbnet_api.h
include $(BUILD_TH3_BINARY)
endif

include $(CLEAR_VARS)
LOCAL_MODULE := mbnet_test
LOCAL_RELATE_MODE := plat
LOCAL_DEPS_MODULES := mbnetlib_a

LOCAL_SRC_FILES := $(call get-all-wildcard-files, \
				./demo, \
				.c)

LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/

LOCAL_STATIC_LIBRARIES := mbnetlib_a
LOCAL_LDLIBS := -lpthread

ifdef TARGET_RELEASE_DIR
LOCAL_RELEASE_PATH := $(TARGET_RELEASE_DIR)/cbb/mbnet/exe/$(HOST_OS)_$(TARGET_PLATFORM)
endif
LOCAL_RELEASE_PATH += $(LOCAL_PATH)/release/exe/$(HOST_OS)_$(TARGET_PLATFORM)
include $(BUILD_EXECUTABLE)

