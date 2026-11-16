CC := gcc
AR := ar

TARGET_NAME := openxr_loader.dll
TARGET_DEBUG = $(OBJDIR_DEBUG)/$(TARGET_NAME)
TARGET_RELEASE = $(OBJDIR_RELEASE)/$(TARGET_NAME)

SRC := src/devices.c src/ui.c src/main.c src/Quaternion.c
DEF_FILE := src/openxr_loader.def

OBJDIR_DEBUG := obj/Debug
OBJDIR_RELEASE := obj/Release

OBJ_DEBUG := $(SRC:%.c=$(OBJDIR_DEBUG)/%.o)
OBJ_RELEASE := $(SRC:%.c=$(OBJDIR_RELEASE)/%.o)

# Override these when needed, for example:
# make dll_release OPENXR_INCLUDE="/c/OpenXR-SDK/include" OPENXR_LIB="/c/OpenXR-SDK/lib" RELEASE_OUTPUT_DLL="/c/Path With Spaces/openxr_loader.dll"
OPENXR_INCLUDE ?=
OPENXR_LIB ?=
DEBUG_OUTPUT_DLL ?=
RELEASE_OUTPUT_DLL ?=

CSTD := -std=c11
COMMON_WARN := -Wall
COMMON_FLAGS := $(COMMON_WARN) -fexceptions -DBUILD_DLL -march=core2 -m64 $(CSTD)

# The vendored Khronos headers are used when nothing else is given.
ifeq ($(strip $(OPENXR_INCLUDE)),)
ifneq ($(wildcard third_party/openxr-sdk/include/openxr/openxr.h),)
OPENXR_INCLUDE := third_party/openxr-sdk/include
endif
endif

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

# -lopengl32 & co: the interface (src/ui.c) links into the proxy too.
LIBS := -lgdi32 -lkernel32 -lpsapi -luser32 -lopengl32 -lshell32 -limm32 -ldwmapi -lcomdlg32 -lole32
LINK_COMMON := -shared -Wl,--dll -static-libstdc++ -static-libgcc -m64

DEBUG_CFLAGS := $(COMMON_FLAGS) -O0 -g3
RELEASE_CFLAGS := $(COMMON_FLAGS) -O2
LAYER_DEMO_CFLAGS := $(COMMON_FLAGS) -O0 -g3

LAYER_DEMO_SRC := src/openxr_layer_demo.c
LAYER_DEMO_OBJDIR := obj/LayerDemo
LAYER_DEMO_OBJ := $(LAYER_DEMO_OBJDIR)/src/openxr_layer_demo.o
LAYER_DEMO_TARGET := $(LAYER_DEMO_OBJDIR)/openxr_api_layer_demo.dll

    # ------------------------------------------------------------------- UI
    #
    # The interface (src/ui.c) only talks to the device model (src/devices.c),
    # so it links into a plain program: no OpenXR, no runtime, no game. This
    # is how the interface gets tried out during development.
    #
    #   make ui_run          open the interface with a made up session
    #   make ui_shot         save obj/Ui/ui.png and exit
    #   make ui_release      the same program, optimised

CUI_DIR := third_party/CUI
UI_OBJDIR := obj/Ui
CUI_LIB := $(UI_OBJDIR)/cui.a

# Everything CUI ships except the windowing backends that do not apply here.
CUI_BACKENDS := $(CUI_DIR)/src/platform/cui_app_x11.c $(CUI_DIR)/src/platform/cui_app_sdl2.c $(CUI_DIR)/src/platform/cui_app_glfw.c
CUI_SRCS := $(filter-out $(CUI_BACKENDS),$(wildcard $(CUI_DIR)/src/*.c) $(wildcard $(CUI_DIR)/src/render/*.c) $(wildcard $(CUI_DIR)/src/platform/*.c))
CUI_OBJS := $(patsubst $(CUI_DIR)/%.c,$(UI_OBJDIR)/cui/%.o,$(CUI_SRCS))

# Third party code is not warned about.
CUI_CFLAGS := -I$(CUI_DIR)/include $(CSTD) -O2 -w

UI_SRC := src/ui_main.c src/ui.c src/devices.c src/Quaternion.c
UI_OBJ := $(patsubst %.c,$(UI_OBJDIR)/%.o,$(UI_SRC))
UI_TARGET_DEBUG := $(UI_OBJDIR)/ui_debug.exe
UI_TARGET_RELEASE := $(UI_OBJDIR)/ui_release.exe

UI_CFLAGS := $(COMMON_FLAGS) $(CUI_CFLAGS)
UI_LIBS := -lopengl32 -luser32 -lgdi32 -lshell32 -limm32 -ldwmapi -lcomdlg32 -lole32
UI_LINK := -m64 -static -static-libgcc

# cmd.exe has no mkdir -p and no rm -rf, and it is make's shell on Windows
# unless a POSIX shell is asked for explicitly.
ifeq ($(OS),Windows_NT)
define MKDIR
if not exist "$(subst /,\,$1)" mkdir "$(subst /,\,$1)"
endef
define RMDIR
if exist "$(subst /,\,$1)" rmdir /s /q "$(subst /,\,$1)"
endef
UI_RUN_SHOT := set CUI_SCREENSHOT=obj/Ui/ui.png&& set CUI_SCREENSHOT_FRAMES=60&&
# cmd.exe will not launch a relative exe path written with slashes
UI_RUN_DEBUG := $(subst /,\,$(UI_TARGET_DEBUG))
UI_RUN_RELEASE := $(subst /,\,$(UI_TARGET_RELEASE))
else
define MKDIR
mkdir -p $1
endef
define RMDIR
rm -rf $1
endef
UI_RUN_SHOT := CUI_SCREENSHOT=obj/Ui/ui.png CUI_SCREENSHOT_FRAMES=60
UI_RUN_DEBUG := $(UI_TARGET_DEBUG)
UI_RUN_RELEASE := $(UI_TARGET_RELEASE)
endif

.PHONY: all dll_debug dll_release layer_demo ui ui_debug ui_release ui_run ui_shot clean

all: dll_release

dll_debug: $(OUT_DEBUG)

dll_release: $(OUT_RELEASE)

layer_demo: $(LAYER_DEMO_TARGET)

ui: ui_debug

ui_debug: $(UI_TARGET_DEBUG)

ui_release: $(UI_TARGET_RELEASE)

# The interface, straight on screen, with a made up session in it.
ui_run: ui_debug
	$(UI_RUN_DEBUG)

# ... and the same thing as a picture, for checking the layout in one go.
ui_shot: ui_debug
	$(UI_RUN_SHOT) $(UI_RUN_DEBUG) --frames 60 --quiet

$(OBJDIR_DEBUG):
	$(call MKDIR,$(OBJDIR_DEBUG))

$(OBJDIR_RELEASE):
	$(call MKDIR,$(OBJDIR_RELEASE))

$(LAYER_DEMO_OBJDIR):
	$(call MKDIR,$(LAYER_DEMO_OBJDIR))

$(UI_OBJDIR):
	$(call MKDIR,$(UI_OBJDIR))

$(OBJDIR_DEBUG)/%.o: %.c | $(OBJDIR_DEBUG)
	$(call MKDIR,$(dir $@))
	$(CC) $(DEBUG_CFLAGS) $(INCLUDE_FLAGS) -I$(CUI_DIR)/include -c $< -o $@

$(OBJDIR_RELEASE)/%.o: %.c | $(OBJDIR_RELEASE)
	$(call MKDIR,$(dir $@))
	$(CC) $(RELEASE_CFLAGS) $(INCLUDE_FLAGS) -I$(CUI_DIR)/include -c $< -o $@

$(LAYER_DEMO_OBJDIR)/%.o: %.c | $(LAYER_DEMO_OBJDIR)
	$(call MKDIR,$(dir $@))
	$(CC) $(LAYER_DEMO_CFLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(UI_OBJDIR)/%.o: %.c | $(UI_OBJDIR)
	$(call MKDIR,$(dir $@))
	$(CC) $(UI_CFLAGS) -c $< -o $@

$(UI_OBJDIR)/cui/%.o: $(CUI_DIR)/%.c | $(UI_OBJDIR)
	$(call MKDIR,$(dir $@))
	$(CC) $(CUI_CFLAGS) -c $< -o $@

$(CUI_LIB): $(CUI_OBJS)
	$(call MKDIR,$(dir $@))
	$(AR) rcs $@ $(CUI_OBJS)

$(UI_TARGET_DEBUG): $(UI_OBJ) $(CUI_LIB)
	$(CC) $(UI_LINK) $^ -o $@ $(UI_LIBS)

$(UI_TARGET_RELEASE): $(UI_OBJ) $(CUI_LIB)
	$(CC) $(UI_LINK) -s $^ -o $@ $(UI_LIBS)

$(OUT_DEBUG): $(OBJ_DEBUG) $(CUI_LIB) $(DEF_FILE)
	$(CC) $(LINK_COMMON) $(LIB_DIR_FLAGS) $(OBJ_DEBUG) $(CUI_LIB) $(DEF_FILE) -o "$@" $(LIBS)

$(OUT_RELEASE): $(OBJ_RELEASE) $(CUI_LIB) $(DEF_FILE)
	$(CC) $(LINK_COMMON) $(LIB_DIR_FLAGS) $(OBJ_RELEASE) $(CUI_LIB) $(DEF_FILE) -o "$@" -s $(LIBS)

$(LAYER_DEMO_TARGET): $(LAYER_DEMO_OBJ)
	$(CC) $(LINK_COMMON) $(LIB_DIR_FLAGS) $(LAYER_DEMO_OBJ) -o "$@"

clean:
	$(call RMDIR,obj)
