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
    
    # Download and install arm64 development libraries manually
    mkdir -p /tmp/arm64-debs
    cd /tmp/arm64-debs
    
    # Download arm64 packages
    wget http://ports.ubuntu.com/pool/main/g/glib2.0/libglib2.0-0_2.72.4-1_arm64.deb
    wget http://ports.ubuntu.com/pool/main/g/glib2.0/libglib2.0-dev_2.72.4-1_arm64.deb
    wget http://ports.ubuntu.com/pool/main/g/gdk-pixbuf2.0/libgdk-pixbuf2.0-0_2.42.8+dfsg-1ubuntu1_arm64.deb
    wget http://ports.ubuntu.com/pool/main/g/gdk-pixbuf2.0/libgdk-pixbuf2.0-dev_2.42.8+dfsg-1ubuntu1_arm64.deb
    wget http://ports.ubuntu.com/pool/main/p/pkgconf/pkgconf_1.8.0-1_arm64.deb || wget http://ports.ubuntu.com/pool/main/p/pkg-config/pkg-config_0.29.2-1ubuntu3_arm64.deb
    
    # Extract arm64 development files
    dpkg-deb -x libglib2.0-dev_2.72.4-1_arm64.deb /
    dpkg-deb -x libgdk-pixbuf2.0-dev_2.42.8+dfsg-1ubuntu1_arm64.deb /
    if [ -f pkgconf_1.8.0-1_arm64.deb ]; then
        dpkg-deb -x pkgconf_1.8.0-1_arm64.deb /
    else
        dpkg-deb -x pkg-config_0.29.2-1ubuntu3_arm64.deb /
    fi
    
    cd /workspace
    rm -rf /tmp/arm64-debs
    
    echo "Installing latest Chafa for aarch64"
    wget $(curl -s https://api.github.com/repos/hpjansson/chafa/releases/latest | grep 'browser_download_url' | grep '.tar.xz' | head -1 | cut -d'"' -f4)
    
    tar -xf chafa-*.tar.xz
    cd $(ls -d chafa-* | head -1)
    
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