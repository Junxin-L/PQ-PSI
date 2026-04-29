FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    autoconf \
    automake \
    build-essential \
    ca-certificates \
    cmake \
    git \
    iproute2 \
    libboost-dev \
    libboost-system-dev \
    libtool \
    libtool-bin \
    libssl-dev \
    pkg-config \
    python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/VOLE-PSI

COPY . .

RUN python3 build.py --par=4 \
    -DNO_ARCH_NATIVE=ON \
    -DVOLE_PSI_ENABLE_BOOST=ON \
    -DVOLE_PSI_ENABLE_SSE=OFF \
    -DVOLE_PSI_ENABLE_BITPOLYMUL=OFF \
    && test -x /opt/VOLE-PSI/out/build/linux/frontend/frontend

WORKDIR /opt/VOLE-PSI

CMD ["./out/build/linux/frontend/frontend", "-u"]
