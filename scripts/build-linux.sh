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
    
    echo "Installing latest Chafa for aarch64"
    wget $(curl -s https://api.github.com/repos/hpjansson/chafa/releases/latest | grep 'browser_download_url' | grep '.tar.xz' | head -1 | cut -d'"' -f4)
    
    tar -xf chafa-*.tar.xz
    cd $(ls -d chafa-* | head -1)
    
    # Cross-compile Chafa for aarch64
    ./configure --prefix=/usr/aarch64-linux-gnu --host=aarch64-linux-gnu CC=aarch64-linux-gnu-gcc \
        --disable-shared --enable-static \
        PKG_CONFIG_PATH=/usr/lib/aarch64-linux-gnu/pkgconfig \
        GLIB_CFLAGS="-I/usr/include/glib-2.0" \
        GLIB_LIBS="-lglib-2.0" \
        GDK_PIXBUF_CFLAGS="-I/usr/include/gdk-pixbuf-2.0" \
        GDK_PIXBUF_LIBS="-lgdk-pixbuf-2.0"
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