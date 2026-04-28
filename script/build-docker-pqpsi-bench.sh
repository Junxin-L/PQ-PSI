#!/usr/bin/env bash

set -euo pipefail

export PATH="/opt/homebrew/bin:/usr/local/bin:/Applications/Docker.app/Contents/Resources/bin:$PATH"

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build-docker}"
CONFIG_LOG="${CONFIG_LOG:-$REPO_ROOT/pqpsi-configure.log}"
BUILD_LOG="${BUILD_LOG:-$REPO_ROOT/pqpsi-build.log}"
CONTAINER_BUILD_DIR="/tmp/pqpsi-build"
CONTAINER_CONFIG_LOG="/work/pqpsi-configure.log"
CONTAINER_BUILD_LOG="/work/pqpsi-build.log"
CONTAINER_MIRACL_ROOT="/work/thirdparty/linux/miracl/miracl"

docker run --rm --platform linux/amd64 \
    -v "$REPO_ROOT:/work" \
    -w /work \
    ubuntu:22.04 \
    bash -lc "apt-get update >/tmp/apt-update.log 2>&1 && \
        apt-get install -y --no-install-recommends build-essential cmake git libboost-system-dev libboost-thread-dev libgmp-dev libgf2x-dev libntl-dev libsodium-dev >/tmp/apt-install.log 2>&1 && \
        rm -rf ${CONTAINER_BUILD_DIR} && \
        bash /work/script/build-miracl-linux64.sh ${CONTAINER_MIRACL_ROOT} >/work/miracl-build.log 2>&1 && \
        cmake -S /work -B ${CONTAINER_BUILD_DIR} -DCMAKE_BUILD_TYPE=Release -DPQPSI_BUILD_RBOKVS_BENCH_ONLY=ON >${CONTAINER_CONFIG_LOG} 2>&1 && \
        cmake --build ${CONTAINER_BUILD_DIR} --target pqpsi_rbokvs_bench rbokvs_g_check rbokvs_pqpsi_test pqpsi_tests construction_permutation_bench --parallel 1 >${CONTAINER_BUILD_LOG} 2>&1"

echo "Built pqpsi_rbokvs_bench, rbokvs_g_check, rbokvs_pqpsi_test, pqpsi_tests, and construction_permutation_bench into $BUILD_DIR"
