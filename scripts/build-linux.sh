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
    
    echo "Installing latest Chafa for aarch64"
    wget $(curl -s https://api.github.com/repos/hpjansson/chafa/releases/latest | grep 'browser_download_url' | grep '.tar.xz' | head -1 | cut -d'"' -f4)
    
    tar -xf chafa-*.tar.xz
    cd $(ls -d chafa-* | head -1)
    
    # Cross-compile Chafa for aarch64 with minimal dependencies
    echo "Running configure with debug info..."
    
    # Test basic compilation without glib
    echo "Testing basic cross-compilation..."
    cat > conftest.c << 'EOF'
int main() { return 0; }
EOF
    aarch64-linux-gnu-gcc -static -o conftest conftest.c 2>&1
    if [ -f conftest ]; then
        echo "Basic cross-compilation successful"
        rm conftest conftest.c
    else
        echo "Basic cross-compilation failed"
        rm -f conftest conftest.c
        exit 1
    fi
    
    # Try to build Chafa without glib dependencies first
    echo "Attempting to build Chafa without external dependencies..."
    ./configure --prefix=/usr/aarch64-linux-gnu --host=aarch64-linux-gnu CC=aarch64-linux-gnu-gcc \
        --disable-shared --enable-static \
        --without-glib --without-gdk-pixbuf \
        LDFLAGS="-static" || {
        echo "Configure failed even without dependencies. Full config.log:"
        if [ -f config.log ]; then
            cat config.log
        fi
        exit 1
    }
    make -j$(nproc)
    make install
    cd ..
    rm -rf chafa-*
    
    # Cross-compile PixelTerm-C for aarch64
    make clean && make CC=aarch64-linux-gnu-gcc ARCH=aarch64
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