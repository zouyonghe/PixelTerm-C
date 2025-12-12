CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -Wno-sign-compare -Wno-unused-variable -Wno-unused-but-set-variable -Wno-switch
DEBUG_CFLAGS = -g -DDEBUG -fsanitize=address
LIBS = $(shell pkg-config --libs chafa) $(shell pkg-config --libs gdk-pixbuf-2.0) -lpthread
INCLUDES = -Iinclude $(shell pkg-config --cflags glib-2.0) $(shell pkg-config --cflags chafa) $(shell pkg-config --cflags gdk-pixbuf-2.0)
SRCDIR = src
OBJDIR = obj
BINDIR = bin

# Source files
SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
TARGET = $(BINDIR)/pixelterm-c

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
	install -D $(TARGET) $(DESTDIR)/usr/local/bin/pixelterm-c

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

.PHONY: all debug clean install test run check-deps help