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
    
    # Get latest Chafa release
    CHAFA_VERSION=$(curl -s https://api.github.com/repos/hpjansson/chafa/releases/latest | grep '"tag_name"' | cut -d'"' -f4)
    if [ -z "$CHAFA_VERSION" ]; then
      CHAFA_VERSION="v1.14.4"
    fi
    echo "Installing Chafa ${CHAFA_VERSION}"
    
    # Install Chafa from source to get the latest version with all required APIs
    wget "https://github.com/hpjansson/chafa/releases/download/${CHAFA_VERSION}/chafa-${CHAFA_VERSION}.tar.xz" ||
    wget "https://github.com/hpjansson/chafa/releases/download/v1.14.4/chafa-v1.14.4.tar.xz"
    
    if [ -f "chafa-${CHAFA_VERSION}.tar.xz" ]; then
      tar -xf chafa-${CHAFA_VERSION}.tar.xz
      cd chafa-${CHAFA_VERSION}
    else
      tar -xf chafa-v1.14.4.tar.xz
      cd chafa-v1.14.4
    fi
    
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