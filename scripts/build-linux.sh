#!/bin/bash

set -e

cd /workspace
export DEBIAN_FRONTEND=noninteractive

if [ "$1" = "aarch64" ]; then
    # Cross-compile for aarch64
    echo "Setting up cross-compilation for aarch64"
    
    # Add arm64 architecture and configure sources
    dpkg --add-architecture arm64
    cat > /etc/apt/sources.list.d/arm64.list << 'EOF'
deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports/ jammy main restricted
deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports/ jammy-updates main restricted
deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports/ jammy-backports main restricted universe multiverse
EOF
    
    apt-get update
    apt-get install -y build-essential libglib2.0-dev libgdk-pixbuf2.0-dev pkg-config wget jq libfreetype6-dev git curl gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
    
    # Install cross-compilation dependencies
    apt-get install -y libglib2.0-dev:arm64 libgdk-pixbuf2.0-dev:arm64 pkg-config:arm64
    
    echo "Installing latest Chafa for aarch64"
    wget $(curl -s https://api.github.com/repos/hpjansson/chafa/releases/latest | grep 'browser_download_url' | grep '.tar.xz' | head -1 | cut -d'"' -f4)
    
    tar -xf chafa-*.tar.xz
    cd chafa-*
    
    # Cross-compile Chafa for aarch64
    ./configure --prefix=/usr/aarch64-linux-gnu --host=aarch64-linux-gnu CC=aarch64-linux-gnu-gcc
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
    cd chafa-*
    
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