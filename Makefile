CC ?= gcc
ARCH ?= amd64
VERSION = $(shell git describe --tags --always --dirty 2>/dev/null || echo "unknown")
CFLAGS = -Wall -Wextra -std=c11 -O2 -Wno-sign-compare -Wno-unused-variable -Wno-unused-but-set-variable -Wno-switch -DAPP_VERSION=\"$(VERSION)\"
DEBUG_CFLAGS = -g -DDEBUG -fsanitize=address

# Cross-compilation settings
ifeq ($(ARCH),aarch64)
  PKG_CONFIG_PATH = /usr/lib/aarch64-linux-gnu/pkgconfig
  LIBS = $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs chafa) $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs gdk-pixbuf-2.0) -lpthread
  INCLUDES = -Iinclude $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags glib-2.0) $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags chafa) $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags gdk-pixbuf-2.0)
else
  LIBS = $(shell pkg-config --libs chafa) $(shell pkg-config --libs gdk-pixbuf-2.0) -lpthread
  INCLUDES = -Iinclude $(shell pkg-config --cflags glib-2.0) $(shell pkg-config --cflags chafa) $(shell pkg-config --cflags gdk-pixbuf-2.0)
endif
SRCDIR = src
OBJDIR = obj
BINDIR = bin

# Source files
SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
TARGET = $(BINDIR)/pixelterm

# Default target
all: $(TARGET)

# Create directories
$(OBJDIR):
	mkdir -p $(OBJDIR)

$(BINDIR):
	mkdir -p $(BINDIR)

# Build the main executable
$(TARGET): $(OBJECTS) | $(BINDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

# Compile source files
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Debug build
debug: CFLAGS += $(DEBUG_CFLAGS)
debug: clean all

# Clean build artifacts
clean:
	rm -rf $(OBJDIR) $(BINDIR)

# Install
install: $(TARGET)
	install -D $(TARGET) $(DESTDIR)/usr/local/bin/pixelterm

# Test
test: $(TARGET)
	@echo "Running basic tests..."
	@echo "No tests defined yet"

# Run with sample image
run: $(TARGET)
	@echo "Run with: ./$(TARGET) [path/to/image/or/directory]"

# Check dependencies
check-deps:
	@echo "Checking dependencies..."
	@pkg-config --exists chafa || (echo "Chafa library not found" && exit 1)
	@pkg-config --exists glib-2.0 || (echo "GLib 2.0 not found" && exit 1)
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