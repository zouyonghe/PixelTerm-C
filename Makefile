CC ?= gcc
ARCH ?= amd64
PREFIX ?= /usr/local
INSTALL ?= install
VERSION = $(shell git describe --tags --exact-match 2>/dev/null || git describe --tags --always --dirty 2>/dev/null | cut -d'-' -f1 | cut -c2- || echo "unknown")
UNAME_S := $(shell uname -s)
# GCC/Clang hardening defaults. Override these variables for toolchains that
# do not support the flags or downstream builds that manage hardening elsewhere.
HARDENING ?= 1
DEBUG ?= 0
DEBUG_HARDENING ?= 0
EXTRA_CFLAGS ?=
OPTIMIZATION_CFLAGS ?= -O2
ifeq ($(HARDENING),1)
  ifeq ($(DEBUG),1)
    ENABLE_HARDENING := $(DEBUG_HARDENING)
  else
    ENABLE_HARDENING := 1
  endif
else
  ENABLE_HARDENING := 0
endif
ifeq ($(ENABLE_HARDENING),1)
  HARDENING_CFLAGS ?= -fstack-protector-strong
  ifeq ($(UNAME_S),Linux)
    HARDENING_LDFLAGS ?= -Wl,-z,relro -Wl,-z,now
  else
    HARDENING_LDFLAGS ?=
  endif
else
  HARDENING_CFLAGS ?=
  FORTIFY_CFLAGS ?=
  HARDENING_LDFLAGS ?=
endif
FORTIFY_LEVEL ?= 2
CFLAGS = -Wall -Wextra -std=c11 $(OPTIMIZATION_CFLAGS) -Wno-sign-compare -Wno-unused-variable -Wno-unused-but-set-variable -Wno-switch -DAPP_VERSION=\"$(VERSION)\"
DEBUG_CFLAGS = -g -DDEBUG -fsanitize=address
DEPFLAGS = -MMD -MP
# Prefer the locally installed Chafa when both system and /usr/local versions exist
LDFLAGS += -Wl,-rpath -Wl,/usr/local/lib

# Cross-compilation settings
ifeq ($(ARCH),aarch64)
  PKG_CONFIG_PATH = /usr/lib/aarch64-linux-gnu/pkgconfig
endif

PKG_CONFIG ?= pkg-config
PKG_CONFIG_CMD = PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) $(PKG_CONFIG)
PKG_DEPS = chafa gdk-pixbuf-2.0 gio-2.0 libavformat libavcodec libswscale libavutil

LIBS = $(shell $(PKG_CONFIG_CMD) --libs $(PKG_DEPS)) -lpthread -lm
INCLUDES = -Iinclude $(shell $(PKG_CONFIG_CMD) --cflags glib-2.0 $(PKG_DEPS))

ifeq ($(UNAME_S),Linux)
  CFLAGS += -ffunction-sections -fdata-sections
  LDFLAGS += -Wl,--gc-sections
else ifeq ($(UNAME_S),Darwin)
  LDFLAGS += -Wl,-dead_strip
endif
ifeq ($(ENABLE_HARDENING),1)
  LDFLAGS += $(HARDENING_LDFLAGS)
endif
ifeq ($(DEBUG),1)
  CFLAGS += $(DEBUG_CFLAGS)
endif
EXTRA_LIBS =
ifneq ($(shell $(PKG_CONFIG_CMD) --exists zlib >/dev/null 2>&1 && echo yes),)
  EXTRA_LIBS += $(shell $(PKG_CONFIG_CMD) --libs zlib)
else
  EXTRA_LIBS += -lz
endif
OPENJP2_PKG =
ifneq ($(shell $(PKG_CONFIG_CMD) --exists openjp2 >/dev/null 2>&1 && echo yes),)
  OPENJP2_PKG = openjp2
else ifneq ($(shell $(PKG_CONFIG_CMD) --exists libopenjp2 >/dev/null 2>&1 && echo yes),)
  OPENJP2_PKG = libopenjp2
endif
ifneq ($(OPENJP2_PKG),)
  EXTRA_LIBS += $(shell $(PKG_CONFIG_CMD) --libs $(OPENJP2_PKG))
endif
ifeq ($(UNAME_S),Linux)
  LIBS += -Wl,--no-as-needed $(EXTRA_LIBS) -Wl,--as-needed
else
  LIBS += $(EXTRA_LIBS)
endif

ifneq ($(shell $(PKG_CONFIG_CMD) --exists mupdf >/dev/null 2>&1 && echo yes),)
  MUPDF_LIBS := $(shell $(PKG_CONFIG_CMD) --libs mupdf)
  HARFBUZZ_LIBS :=
  ifneq ($(shell $(PKG_CONFIG_CMD) --exists harfbuzz >/dev/null 2>&1 && echo yes),)
    HARFBUZZ_LIBS := $(shell $(PKG_CONFIG_CMD) --libs harfbuzz)
  endif
  LIBS += $(MUPDF_LIBS) $(HARFBUZZ_LIBS)
  INCLUDES += $(shell $(PKG_CONFIG_CMD) --cflags mupdf)
  CFLAGS += -DHAVE_MUPDF
else
  MUPDF_PREFIXES = /usr/local /usr/local/opt/mupdf /opt/homebrew/opt/mupdf
  MUPDF_PREFIX ?= $(firstword $(foreach p,$(MUPDF_PREFIXES),$(if $(wildcard $(p)/include/mupdf/fitz.h),$(p),)))
  ifneq ($(MUPDF_PREFIX),)
    FREETYPE_LIBS =
    ifneq ($(shell $(PKG_CONFIG_CMD) --exists freetype2 >/dev/null 2>&1 && echo yes),)
      FREETYPE_LIBS = $(shell $(PKG_CONFIG_CMD) --libs freetype2)
    else
      FREETYPE_LIBS = -lfreetype
    endif
    HARFBUZZ_LIBS =
    ifneq ($(shell $(PKG_CONFIG_CMD) --exists harfbuzz >/dev/null 2>&1 && echo yes),)
      HARFBUZZ_LIBS = $(shell $(PKG_CONFIG_CMD) --libs harfbuzz)
    else
      HARFBUZZ_LIBS = -lharfbuzz
    endif
    MUPDF_LIBDIR ?= $(firstword $(foreach p,$(MUPDF_PREFIX)/lib $(MUPDF_PREFIX)/lib64,$(if $(wildcard $(p)/libmupdf.so)$(wildcard $(p)/libmupdf.a),$(p),)))
    ifeq ($(MUPDF_LIBDIR),)
      MUPDF_LIBDIR = $(MUPDF_PREFIX)/lib
    endif
    MUPDF_THIRD_LIBS =
    ifneq ($(wildcard $(MUPDF_LIBDIR)/libmupdf-third.*),)
      MUPDF_THIRD_LIBS = -lmupdf-third
    endif
    LIBS += -L$(MUPDF_LIBDIR) -lmupdf $(MUPDF_THIRD_LIBS) -ljpeg $(FREETYPE_LIBS) $(HARFBUZZ_LIBS)
    INCLUDES += -I$(MUPDF_PREFIX)/include
    CFLAGS += -DHAVE_MUPDF
  else
    $(warning mupdf not found; building without book support)
  endif
endif
FORTIFY_SOURCE_FLAGS := $(CFLAGS) $(EXTRA_CFLAGS) $(CPPFLAGS)
# Any -O* optimization flag counts as optimized for default FORTIFY.
FORTIFY_OPT_FLAGS := $(filter -O%,$(FORTIFY_SOURCE_FLAGS))
FORTIFY_O0_FLAGS := $(filter -O0,$(FORTIFY_SOURCE_FLAGS))
ifeq ($(ENABLE_HARDENING),1)
  ifneq ($(findstring _FORTIFY_SOURCE,$(FORTIFY_SOURCE_FLAGS)),)
    FORTIFY_CFLAGS ?=
  else ifneq ($(FORTIFY_O0_FLAGS),)
    FORTIFY_CFLAGS ?=
  else ifneq ($(FORTIFY_OPT_FLAGS),)
    FORTIFY_CFLAGS ?= -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=$(FORTIFY_LEVEL)
  else
    FORTIFY_CFLAGS ?=
  endif
else
  FORTIFY_CFLAGS ?=
endif
ifeq ($(ENABLE_HARDENING),1)
  # _FORTIFY_SOURCE needs optimization; keep OPTIMIZATION_CFLAGS at -O1 or higher for normal builds.
  CFLAGS += $(HARDENING_CFLAGS) $(FORTIFY_CFLAGS)
endif
SRCDIR = src
OBJDIR = obj
BINDIR = bin
DEBUG_OBJDIR ?= obj-debug
DEBUG_BINDIR ?= bin-debug
BUILD_FLAGS_FILE = $(OBJDIR)/.build-flags

# Source files
SOURCES = $(wildcard $(SRCDIR)/*.c)
SOURCES := $(filter-out $(SRCDIR)/video_player.c, $(SOURCES))
SOURCES += $(SRCDIR)/video_player.c
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
TARGET = $(BINDIR)/pixelterm
TEST_TARGET = $(BINDIR)/pixelterm-tests
FILE_MANAGER_TEST_TARGET = $(BINDIR)/pixelterm-file-manager-tests
PREVIEW_GRID_TEST_TARGET = $(BINDIR)/pixelterm-preview-grid-tests
BOOK_PREVIEW_TEST_TARGET = $(BINDIR)/pixelterm-book-preview-tests
INSTALL_SCRIPT_TEST = PYTHONDONTWRITEBYTECODE=1 python3 scripts/test_install_script.py
TEST_SOURCES = $(filter-out tests/test_app_file_manager.c tests/test_app_preview_grid.c tests/test_app_preview_book.c, $(wildcard tests/test_*.c))
TEST_OBJECTS = $(TEST_SOURCES:tests/%.c=$(OBJDIR)/%.o)
FILE_MANAGER_TEST_SOURCE = tests/test_app_file_manager.c
FILE_MANAGER_TEST_OBJECT = $(OBJDIR)/test_app_file_manager.o
PREVIEW_GRID_TEST_SOURCE = tests/test_app_preview_grid.c
PREVIEW_GRID_TEST_OBJECT = $(OBJDIR)/test_app_preview_grid.o
BOOK_PREVIEW_TEST_SOURCE = tests/test_app_preview_book.c
BOOK_PREVIEW_TEST_OBJECT = $(OBJDIR)/test_app_preview_book.o
TEST_COMMON_LINK_OBJECTS = $(OBJDIR)/common.o $(OBJDIR)/text_utils.o $(OBJDIR)/process_env.o \
		$(OBJDIR)/ui_render_utils.o
TEST_RENDER_LINK_OBJECTS = $(OBJDIR)/browser.o $(OBJDIR)/renderer.o $(OBJDIR)/pixbuf_utils.o \
		$(OBJDIR)/kitty_graphics.o
TEST_MEDIA_LINK_OBJECTS = $(OBJDIR)/gif_player.o $(OBJDIR)/media_buffer.o $(OBJDIR)/preloader.o \
		$(OBJDIR)/app_media_session.o $(OBJDIR)/media_utils.o \
		$(OBJDIR)/video_player_clock.o $(OBJDIR)/video_player_debug.o $(OBJDIR)/video_player_decode.o \
		$(OBJDIR)/video_player_layout.o $(OBJDIR)/video_player_playback.o \
		$(OBJDIR)/video_player_seek.o $(OBJDIR)/video_player.o
TEST_INPUT_LINK_OBJECTS = $(OBJDIR)/input.o $(OBJDIR)/input_dispatch_pending_clicks.o \
		$(OBJDIR)/input_dispatch_delete.o $(OBJDIR)/input_dispatch_core.o \
		$(OBJDIR)/input_dispatch_key_single.o $(OBJDIR)/input_dispatch_key_book.o \
		$(OBJDIR)/input_dispatch_key_file_manager.o $(OBJDIR)/input_dispatch_mouse_modes.o
TEST_APP_LINK_OBJECTS = $(OBJDIR)/app_mode.o $(OBJDIR)/app_preview_shared.o \
		$(OBJDIR)/app_single_render.o $(OBJDIR)/app_config_runtime.o $(OBJDIR)/app_cli.o \
		$(OBJDIR)/book.o $(OBJDIR)/app_startup.o
TEST_TERMINAL_LINK_OBJECTS = $(OBJDIR)/terminal_probe.o $(OBJDIR)/terminal_protocols.o \
		$(OBJDIR)/terminal_protocol_resolver.o
TEST_LINK_OBJECTS = $(TEST_COMMON_LINK_OBJECTS) $(TEST_RENDER_LINK_OBJECTS) \
		$(TEST_MEDIA_LINK_OBJECTS) $(TEST_INPUT_LINK_OBJECTS) $(TEST_APP_LINK_OBJECTS) \
		$(TEST_TERMINAL_LINK_OBJECTS)
FILE_MANAGER_TEST_LINK_OBJECTS = $(TEST_COMMON_LINK_OBJECTS) $(OBJDIR)/app_core.o \
		$(OBJDIR)/app_mode.o $(OBJDIR)/app_file_manager.o $(OBJDIR)/app_file_manager_render.o
PREVIEW_GRID_TEST_LINK_OBJECTS = $(OBJDIR)/app_preview_grid.o $(OBJDIR)/ui_render_utils.o $(OBJDIR)/text_utils.o
BOOK_PREVIEW_TEST_LINK_OBJECTS = $(OBJDIR)/app_preview_book.o $(OBJDIR)/app_book_page_render.o

# Default target
all: $(TARGET)

# Create directories
$(OBJDIR):
	mkdir -p $(OBJDIR)

$(BINDIR):
	mkdir -p $(BINDIR)

$(BUILD_FLAGS_FILE): FORCE | $(OBJDIR)
	@{ \
		tmp_file="$@.$$$$.tmp"; \
		printf '%s\n' \
			'CC=$(CC)' \
			'CFLAGS=$(CFLAGS)' \
			'EXTRA_CFLAGS=$(EXTRA_CFLAGS)' \
			'INCLUDES=$(INCLUDES)' \
			'LDFLAGS=$(LDFLAGS)' \
			'LIBS=$(LIBS)' > "$${tmp_file}"; \
		if [ ! -f $@ ] || ! cmp -s "$${tmp_file}" $@; then mv "$${tmp_file}" $@; else rm -f "$${tmp_file}"; fi; \
	}

# Build the main executable
$(TARGET): $(OBJECTS) $(BUILD_FLAGS_FILE) | $(BINDIR)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(INCLUDES) $(LDFLAGS) -o $@ $(OBJECTS) $(LIBS)

# Compile source files
$(OBJDIR)/%.o: $(SRCDIR)/%.c $(BUILD_FLAGS_FILE) | $(OBJDIR)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(INCLUDES) $(DEPFLAGS) -c $< -o $@

# Compile test sources
$(OBJDIR)/test_%.o: tests/test_%.c $(BUILD_FLAGS_FILE) | $(OBJDIR)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(INCLUDES) $(DEPFLAGS) -c $< -o $@

# Build test executable
$(TEST_TARGET): $(TEST_OBJECTS) $(TEST_LINK_OBJECTS) $(BUILD_FLAGS_FILE) | $(BINDIR)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(INCLUDES) $(LDFLAGS) -o $@ $(TEST_OBJECTS) $(TEST_LINK_OBJECTS) $(LIBS)

$(FILE_MANAGER_TEST_TARGET): $(FILE_MANAGER_TEST_OBJECT) $(FILE_MANAGER_TEST_LINK_OBJECTS) $(BUILD_FLAGS_FILE) | $(BINDIR)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(INCLUDES) $(LDFLAGS) -o $@ $(FILE_MANAGER_TEST_OBJECT) $(FILE_MANAGER_TEST_LINK_OBJECTS) $(LIBS)

$(PREVIEW_GRID_TEST_TARGET): $(PREVIEW_GRID_TEST_OBJECT) $(PREVIEW_GRID_TEST_LINK_OBJECTS) $(BUILD_FLAGS_FILE) | $(BINDIR)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(INCLUDES) $(LDFLAGS) -o $@ $(PREVIEW_GRID_TEST_OBJECT) $(PREVIEW_GRID_TEST_LINK_OBJECTS) $(LIBS)

$(BOOK_PREVIEW_TEST_TARGET): $(BOOK_PREVIEW_TEST_OBJECT) $(BOOK_PREVIEW_TEST_LINK_OBJECTS) $(BUILD_FLAGS_FILE) | $(BINDIR)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(INCLUDES) $(LDFLAGS) -o $@ $(BOOK_PREVIEW_TEST_OBJECT) $(BOOK_PREVIEW_TEST_LINK_OBJECTS) $(LIBS)

# Debug build
debug:
	$(MAKE) OBJDIR="$(DEBUG_OBJDIR)" BINDIR="$(DEBUG_BINDIR)" DEBUG=1 EXTRA_CFLAGS="$(EXTRA_CFLAGS)" all

debug-test:
	$(MAKE) OBJDIR="$(DEBUG_OBJDIR)" BINDIR="$(DEBUG_BINDIR)" DEBUG=1 EXTRA_CFLAGS="$(EXTRA_CFLAGS)" test

# Clean build artifacts
clean:
	rm -rf $(OBJDIR) $(BINDIR) $(DEBUG_OBJDIR) $(DEBUG_BINDIR)

# Install
install: $(TARGET)
	$(INSTALL) -d "$(DESTDIR)$(PREFIX)/bin"
	$(INSTALL) -m 0755 "$(TARGET)" "$(DESTDIR)$(PREFIX)/bin/pixelterm"

# Test
test: $(TEST_TARGET) $(FILE_MANAGER_TEST_TARGET) $(PREVIEW_GRID_TEST_TARGET) $(BOOK_PREVIEW_TEST_TARGET)
	@echo "Running tests..."
	@$(TEST_TARGET)
	@$(FILE_MANAGER_TEST_TARGET)
	@$(PREVIEW_GRID_TEST_TARGET)
	@$(BOOK_PREVIEW_TEST_TARGET)
	@$(INSTALL_SCRIPT_TEST)

# Run with sample image
run: $(TARGET)
	@if [ -n "$(ARGS)" ]; then ./$(TARGET) $(ARGS); else ./$(TARGET); fi

# Check dependencies
check-deps:
	@echo "Checking dependencies..."
	@pkg-config --exists chafa || (echo "Chafa library not found" && exit 1)
	@pkg-config --exists glib-2.0 || (echo "GLib 2.0 not found" && exit 1)
	@pkg-config --exists libavformat || (echo "FFmpeg libavformat not found" && exit 1)
	@pkg-config --exists libavcodec || (echo "FFmpeg libavcodec not found" && exit 1)
	@pkg-config --exists libswscale || (echo "FFmpeg libswscale not found" && exit 1)
	@pkg-config --exists libavutil || (echo "FFmpeg libavutil not found" && exit 1)
	@echo "All dependencies found"

# Help
help:
	@echo "Available targets:"
	@echo "  all       - Build the application (default)"
	@echo "  debug     - Build with debug flags"
	@echo "  debug-test - Run tests with debug AddressSanitizer flags"
	@echo "  clean     - Remove build artifacts"
	@echo "  install   - Install to system"
	@echo "  test      - Run tests"
	@echo "  run       - Build and run (use ARGS=... to pass args)"
	@echo "  check-deps- Check dependencies"
	@echo "  help      - Show this help"
	@echo ""
	@echo "Architecture options:"
	@echo "  ARCH=amd64   - Build for x86_64 (default)"
	@echo "  ARCH=aarch64 - Cross-compile for ARM64"
	@echo ""
	@echo "Examples:"
	@echo "  make                     # Build for x86_64"
	@echo "  make ARCH=aarch64        # Cross-compile for ARM64"
	@echo "  make CC=aarch64-linux-gnu-gcc ARCH=aarch64  # Full cross-compilation"
	@echo "  make run ARGS=\"/path/to/image.jpg\"  # Run with args"

.PHONY: FORCE all debug debug-test clean install test run check-deps help

FORCE:

# Auto-generated dependencies
-include $(OBJECTS:.o=.d) $(TEST_OBJECTS:.o=.d) $(FILE_MANAGER_TEST_OBJECT:.o=.d) $(PREVIEW_GRID_TEST_OBJECT:.o=.d) $(BOOK_PREVIEW_TEST_OBJECT:.o=.d)
