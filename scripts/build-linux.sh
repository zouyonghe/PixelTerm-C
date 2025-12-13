#!/bin/bash

set -e

cd /workspace
export DEBIAN_FRONTEND=noninteractive

if [ "$1" = "aarch64" ]; then
    # Cross-compile for aarch64
    echo "Setting up cross-compilation for aarch64"
    
    # Install cross-compilation tools
    apt-get update
    apt-get install -y build-essential libglib2.0-dev libgdk-pixbuf2.0-dev pkg-config wget jq libfreetype6-dev git curl gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
    
    # Install aarch64 libc and development files
    apt-get install -y libc6-dev-arm64-cross linux-libc-dev-arm64-cross
    
    # Test cross-compiler
    echo "Testing aarch64 cross-compiler..."
    aarch64-linux-gnu-gcc --version
    echo '#include <stdio.h>
int main() { printf("Hello World\n"); return 0; }' | aarch64-linux-gnu-gcc -x c - -o /tmp/test-aarch64
    if [ -f /tmp/test-aarch64 ]; then
        echo "Cross-compiler test successful"
        rm /tmp/test-aarch64
    else
        echo "Cross-compiler test failed"
        exit 1
    fi
    
    echo "Using pre-compiled Chafa for aarch64 (skip cross-compilation)"
    # Download pre-compiled Chafa binary if available, otherwise skip Chafa
    # This is a workaround for cross-compilation issues
    echo "Warning: Skipping Chafa cross-compilation due to header conflicts"
    echo "PixelTerm-C will need to be linked against system Chafa at runtime"
    
    # Cross-compile PixelTerm-C for aarch64 (dynamic linking)
    make clean && make CC=aarch64-linux-gnu-gcc ARCH=aarch64 LIBS="-lchafa -lgdk-pixbuf-2.0 -lglib-2.0 -lpthread"
    cp bin/pixelterm /workspace/release-aarch64/pixelterm-aarch64
    
else
    # Native build for amd64
    echo "Native build for amd64"
    
    apt-get update
    apt-get install -y build-essential libglib2.0-dev libgdk-pixbuf2.0-dev pkg-config wget jq libfreetype6-dev git curl
    
    echo "Installing latest Chafa for amd64"
    wget $(curl -s https://api.github.com/repos/hpjansson/chafa/releases/latest | grep 'browser_download_url' | grep '.tar.xz' | head -1 | cut -d'"' -f4)
    
    tar -xf chafa-*.tar.xz
    cd $(ls -d chafa-* | head -1)
    
    ./configure --prefix=/usr
    make -j$(nproc)
    make install
    ldconfig
    cd ..
    rm -rf chafa-*
    
    make clean && make
    cp bin/pixelterm /workspace/release-amd64/pixelterm-amd64
fi

echo "Build completed successfully"