FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update >/dev/null && \
    apt-get install -y --no-install-recommends \
        bash coreutils gawk grep sed python3 \
        iproute2 util-linux procps \
        libboost-system1.74.0 libboost-thread1.74.0 \
        libgmp10 libsodium23 >/dev/null && \
    rm -rf /var/lib/apt/lists/*
