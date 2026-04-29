CC := gcc

TARGET_NAME := openxr_loader.dll
TARGET_DEBUG = $(OBJDIR_DEBUG)/$(TARGET_NAME)
TARGET_RELEASE = $(OBJDIR_RELEASE)/$(TARGET_NAME)

SRC := src/gui.c src/main.c src/Quaternion.c
DEF_FILE := src/openxr_loader.def

OBJDIR_DEBUG := obj/Debug
OBJDIR_RELEASE := obj/Release

OBJ_DEBUG := $(SRC:%.c=$(OBJDIR_DEBUG)/%.o)
OBJ_RELEASE := $(SRC:%.c=$(OBJDIR_RELEASE)/%.o)

# Override these when needed, for example:
# make dll_release OPENXR_INCLUDE="/ucrt64/include" OPENXR_LIB="/ucrt64/lib" RELEASE_OUTPUT_DLL="/c/Path With Spaces/openxr_loader.dll"
OPENXR_INCLUDE ?=
OPENXR_LIB ?=
DEBUG_OUTPUT_DLL ?=
RELEASE_OUTPUT_DLL ?=

CSTD := -std=c11
COMMON_WARN := -Wall
COMMON_FLAGS := $(COMMON_WARN) -fexceptions -DBUILD_DLL -march=core2 -m64 $(CSTD)

ifeq ($(strip $(OPENXR_INCLUDE)),)
INCLUDE_FLAGS :=
else
INCLUDE_FLAGS := -I$(OPENXR_INCLUDE)
endif

ifeq ($(strip $(OPENXR_LIB)),)
LIB_DIR_FLAGS :=
else
LIB_DIR_FLAGS := -L$(OPENXR_LIB)
endif

ifeq ($(strip $(DEBUG_OUTPUT_DLL)),)
OUT_DEBUG := $(TARGET_DEBUG)
else
OUT_DEBUG := $(DEBUG_OUTPUT_DLL)
endif

ifeq ($(strip $(RELEASE_OUTPUT_DLL)),)
OUT_RELEASE := $(TARGET_RELEASE)
else
OUT_RELEASE := $(RELEASE_OUTPUT_DLL)
endif

LIBS := -lgdi32 -lkernel32 -lpsapi -luser32
LINK_COMMON := -shared -Wl,--dll -static-libstdc++ -static-libgcc -m64

DEBUG_CFLAGS := $(COMMON_FLAGS) -O0 -g3
RELEASE_CFLAGS := $(COMMON_FLAGS) -O2
LAYER_DEMO_CFLAGS := $(COMMON_FLAGS) -O0 -g3

LAYER_DEMO_SRC := src/openxr_layer_demo.c
LAYER_DEMO_OBJDIR := obj/LayerDemo
LAYER_DEMO_OBJ := $(LAYER_DEMO_OBJDIR)/src/openxr_layer_demo.o
LAYER_DEMO_TARGET := $(LAYER_DEMO_OBJDIR)/openxr_api_layer_demo.dll

.PHONY: all dll_debug dll_release layer_demo clean

all: dll_release

dll_debug: $(OUT_DEBUG)

dll_release: $(OUT_RELEASE)

layer_demo: $(LAYER_DEMO_TARGET)

$(OBJDIR_DEBUG):
	mkdir -p $(OBJDIR_DEBUG)

$(OBJDIR_RELEASE):
	mkdir -p $(OBJDIR_RELEASE)

$(LAYER_DEMO_OBJDIR):
	mkdir -p $(LAYER_DEMO_OBJDIR)

$(OBJDIR_DEBUG)/%.o: %.c | $(OBJDIR_DEBUG)
	mkdir -p $(dir $@)
	$(CC) $(DEBUG_CFLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(OBJDIR_RELEASE)/%.o: %.c | $(OBJDIR_RELEASE)
	mkdir -p $(dir $@)
	$(CC) $(RELEASE_CFLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(LAYER_DEMO_OBJDIR)/%.o: %.c | $(LAYER_DEMO_OBJDIR)
	mkdir -p $(dir $@)
	$(CC) $(LAYER_DEMO_CFLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(OUT_DEBUG): $(OBJ_DEBUG) $(DEF_FILE)
	$(CC) $(LINK_COMMON) $(LIB_DIR_FLAGS) $(OBJ_DEBUG) $(DEF_FILE) -o "$@" $(LIBS)

$(OUT_RELEASE): $(OBJ_RELEASE) $(DEF_FILE)
	$(CC) $(LINK_COMMON) $(LIB_DIR_FLAGS) $(OBJ_RELEASE) $(DEF_FILE) -o "$@" -s $(LIBS)

$(LAYER_DEMO_TARGET): $(LAYER_DEMO_OBJ)
	$(CC) $(LINK_COMMON) $(LIB_DIR_FLAGS) $(LAYER_DEMO_OBJ) -o "$@"

clean:
	rm -rf obj
