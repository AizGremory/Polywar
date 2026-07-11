LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := libdobby
LOCAL_SRC_FILES := libraries/$(TARGET_ARCH_ABI)/libdobby.a
include $(PREBUILT_STATIC_LIBRARY)


include $(CLEAR_VARS)
LOCAL_MODULE    := MyLibName

# -std=c++17 is required to support AIDE app with NDK
LOCAL_CFLAGS := -w -Wno-error=format-security -fvisibility=hidden -fpermissive -fexceptions
LOCAL_CFLAGS += -g3 -fno-omit-frame-pointer -funwind-tables
LOCAL_CPPFLAGS := -w -Wno-error=format-security -fvisibility=hidden -Werror -std=c++17
LOCAL_CPPFLAGS += -Wno-error=c++11-narrowing -fpermissive -Wall -fexceptions
LOCAL_CPPFLAGS += -g3 -fno-omit-frame-pointer -funwind-tables
LOCAL_LDFLAGS += -Wl,--gc-sections,--build-id=sha1
# Link against Android system libs and GLES (adjust GLES version if needed)
LOCAL_LDLIBS := -llog -landroid -lEGL -lGLESv2 -lGLESv3 -lz
LOCAL_ARM_MODE := arm

LOCAL_C_INCLUDES += $(LOCAL_PATH)
LOCAL_C_INCLUDES += $(LOCAL_PATH)/Includes/
LOCAL_C_INCLUDES += $(LOCAL_PATH)/Starcoolxdl/xdl
LOCAL_C_INCLUDES += $(LOCAL_PATH)/imgui
LOCAL_C_INCLUDES += $(LOCAL_PATH)/imgui/backends

IMGUI_SRC_FILES := $(wildcard $(LOCAL_PATH)/imgui/*.cpp)
IMGUI_SRC_FILES += $(wildcard $(LOCAL_PATH)/imgui/backends/*.cpp)
IMGUI_SRC_FILES := $(IMGUI_SRC_FILES:$(LOCAL_PATH)/%=%)


# Here you add the cpp file to compile
LOCAL_SRC_FILES := Main.cpp \
	NativeDebug.cpp \
	Substrate/hde64.c \
	Substrate/SubstrateDebug.cpp \
	Substrate/SubstrateHook.cpp \
	Substrate/SubstratePosixMemory.cpp \
	Substrate/SymbolFinder.cpp \
	KittyMemory/KittyMemory.cpp \
	KittyMemory/MemoryPatch.cpp \
    KittyMemory/MemoryBackup.cpp \
    KittyMemory/KittyUtils.cpp \
	And64InlineHook/And64InlineHook.cpp \
	ByNameModding/Tools.cpp \
    ByNameModding/fake_dlfcn.cpp \
    ByNameModding/Il2Cpp.cpp \
	Il2Cpp/il2cpp_dump.cpp \
	Starcoolxdl/xdl.c \
    Starcoolxdl/xdl_iterate.c \
    Starcoolxdl/xdl_linker.c \
    Starcoolxdl/xdl_lzma.c \
    Starcoolxdl/xdl_util.c \
	$(IMGUI_SRC_FILES)
	
LOCAL_STATIC_LIBRARIES := libdobby
include $(BUILD_SHARED_LIBRARY)
