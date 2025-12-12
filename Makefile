CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -g
LIBS = -lchafa -lglib-2.0 -lpthread
INCLUDES = -Iinclude
SRCDIR = src
BUILDDIR = build

SOURCES = $(SRCDIR)/main.c $(SRCDIR)/app.c $(SRCDIR)/browser.c $(SRCDIR)/renderer.c $(SRCDIR)/input.c $(SRCDIR)/preloader.c
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
TARGET = pixelterm-c

.PHONY: all clean debug test install

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	@mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

debug: CFLAGS += -DDEBUG -fsanitize=address -fsanitize=thread
debug: clean all

test: $(TARGET)
	@echo "Running basic functionality tests..."
	./$(TARGET) --help

clean:
	rm -rf $(BUILDDIR) $(TARGET)

install: $(TARGET)
	install -D $(TARGET) $(DESTDIR)/usr/local/bin/$(TARGET)

deps:
	@echo "Checking dependencies..."
	@pkg-config --exists chafa || (echo "Chafa development libraries not found" && exit 1)
	@pkg-config --exists glib-2.0 || (echo "GLib 2.0 development libraries not found" && exit 1)
	@echo "All dependencies found!"

help:
	@echo "Available targets:"
	@echo "  all      - Build the application"
	@echo "  debug    - Build with debug flags and sanitizers"
	@echo "  test     - Run basic tests"
	@echo "  clean    - Remove build artifacts"
	@echo "  install  - Install to system"
	@echo "  deps     - Check build dependencies"
	@echo "  help     - Show this help message"
