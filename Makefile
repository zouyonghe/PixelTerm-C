CC ?= gcc
ARCH ?= amd64
PREFIX ?= /usr/local
INSTALL ?= install
VERSION = $(shell git describe --tags --exact-match 2>/dev/null || git describe --tags --always --dirty 2>/dev/null | cut -d'-' -f1 | cut -c2- || echo "unknown")
CFLAGS = -Wall -Wextra -std=c11 -O2 -Wno-sign-compare -Wno-unused-variable -Wno-unused-but-set-variable -Wno-switch -DAPP_VERSION=\"$(VERSION)\"
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

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
  CFLAGS += -ffunction-sections -fdata-sections
  LDFLAGS += -Wl,--gc-sections
else ifeq ($(UNAME_S),Darwin)
  LDFLAGS += -Wl,-dead_strip
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
  MUPDF_DEPS := $(shell $(PKG_CONFIG_CMD) --libs --static mupdf)
  HARFBUZZ_LIBS :=
  ifneq ($(shell $(PKG_CONFIG_CMD) --exists harfbuzz >/dev/null 2>&1 && echo yes),)
    HARFBUZZ_LIBS := $(shell $(PKG_CONFIG_CMD) --libs harfbuzz)
  endif
  LIBS += $(MUPDF_LIBS) $(MUPDF_DEPS) $(HARFBUZZ_LIBS)
  INCLUDES += $(shell $(PKG_CONFIG_CMD) --cflags mupdf)
  CFLAGS += -DHAVE_MUPDF
else
  MUPDF_PREFIX ?= $(firstword $(wildcard /opt/homebrew/opt/mupdf /usr/local/opt/mupdf))
  ifneq ($(wildcard $(MUPDF_PREFIX)/include/mupdf/fitz.h),)
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
    LIBS += -L$(MUPDF_PREFIX)/lib -lmupdf -lmupdf-third -ljpeg $(FREETYPE_LIBS) $(HARFBUZZ_LIBS)
    INCLUDES += -I$(MUPDF_PREFIX)/include
    CFLAGS += -DHAVE_MUPDF
  else
    $(warning mupdf not found; building without book support)
  endif
endif
SRCDIR = src
OBJDIR = obj
BINDIR = bin

# Source files
SOURCES = $(wildcard $(SRCDIR)/*.c)
SOURCES := $(filter-out $(SRCDIR)/video_player.c, $(SOURCES))
SOURCES += $(SRCDIR)/video_player.c
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
TARGET = $(BINDIR)/pixelterm
TEST_TARGET = $(BINDIR)/pixelterm-tests

# Default target
all: $(TARGET)

# Create directories
$(OBJDIR):
	mkdir -p $(OBJDIR)

$(BINDIR):
	mkdir -p $(BINDIR)

# Build the main executable
$(TARGET): $(OBJECTS) | $(BINDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $(LDFLAGS) -o $@ $^ $(LIBS)

# Compile source files
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) -c $< -o $@

# Compile test sources
$(OBJDIR)/test_common.o: tests/test_common.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) -c $< -o $@

# Compile test sources
$(OBJDIR)/test_browser.o: tests/test_browser.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) -c $< -o $@

# Compile test sources
$(OBJDIR)/test_gif_player.o: tests/test_gif_player.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) -c $< -o $@

# Compile test sources
$(OBJDIR)/test_renderer.o: tests/test_renderer.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) -c $< -o $@

# Build test executable
$(TEST_TARGET): $(OBJDIR)/test_common.o $(OBJDIR)/test_browser.o $(OBJDIR)/test_gif_player.o $(OBJDIR)/test_renderer.o $(OBJDIR)/common.o $(OBJDIR)/browser.o $(OBJDIR)/renderer.o $(OBJDIR)/gif_player.o $(OBJDIR)/video_player.o $(OBJDIR)/input.o | $(BINDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $(LDFLAGS) -o $@ $^ $(LIBS)

# Debug build
debug: CFLAGS += $(DEBUG_CFLAGS)
debug: clean all

# Clean build artifacts
clean:
	rm -rf $(OBJDIR) $(BINDIR)

# Install
install: $(TARGET)
	$(INSTALL) -d "$(DESTDIR)$(PREFIX)/bin"
	$(INSTALL) -m 0755 "$(TARGET)" "$(DESTDIR)$(PREFIX)/bin/pixelterm"

# Test
test: $(TEST_TARGET)
	@echo "Running tests..."
	@$(TEST_TARGET)

# Run with sample image
run: $(TARGET)
	@echo "Run with: ./$(TARGET) [path/to/image/or/directory]"

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
	@echo "  clean     - Remove build artifacts"
	@echo "  install   - Install to system"
	@echo "  test      - Run tests"
	@echo "  run       - Show run instructions"
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

.PHONY: all debug clean install test run check-deps help

# Auto-generated dependencies
-include $(OBJECTS:.o=.d) $(OBJDIR)/test_common.d $(OBJDIR)/test_browser.d \
  $(OBJDIR)/test_gif_player.d $(OBJDIR)/test_renderer.d
