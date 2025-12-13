#!/bin/bash

set -e

# Use current directory instead of /workspace for macOS
mkdir -p release-${1}

# Install dependencies using Homebrew
brew install glib gdk-pixbuf chafa pkg-config wget

# Chafa is already installed by Homebrew
echo "Using Homebrew Chafa"

# Build PixelTerm-C
make clean && make
cp bin/pixelterm release-${1}/pixelterm-${1}-macos

echo "macOS build completed successfully"