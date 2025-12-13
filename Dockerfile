# Dockerfile for building PixelTerm-C
ARG TARGETPLATFORM
ARG BUILDPLATFORM

FROM --platform=$TARGETPLATFORM ubuntu:22.04

# Prevent interactive prompts during installation
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    libglib2.0-dev \
    libgdk-pixbuf2.0-dev \
    pkg-config \
    wget \
    jq \
    libfreetype6-dev \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy source code
COPY . .

# Get latest Chafa release and install
RUN CHAFA_VERSION=$(curl -s https://api.github.com/repos/hpjansson/chafa/releases/latest | grep '"tag_name"' | cut -d'"' -f4) && \
    echo "Installing Chafa ${CHAFA_VERSION}" && \
    wget "https://github.com/hpjansson/chafa/releases/download/${CHAFA_VERSION}/chafa-${CHAFA_VERSION}.tar.xz" && \
    tar -xf chafa-${CHAFA_VERSION}.tar.xz && \
    cd chafa-${CHAFA_VERSION} && \
    ./configure --prefix=/usr && \
    make -j$(nproc) && \
    make install && \
    ldconfig && \
    cd .. && \
    rm -rf chafa-${CHAFA_VERSION} chafa-${CHAFA_VERSION}.tar.xz

# Build the project
RUN make clean && make

# Create output directory
RUN mkdir -p /app/output
RUN cp bin/pixelterm /app/output/
