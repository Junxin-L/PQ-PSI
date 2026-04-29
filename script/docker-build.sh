#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE_NAME="${IMAGE_NAME:-vole-psi-linux}"
DOCKER_PLATFORM="${DOCKER_PLATFORM:-}"

if [ -n "${DOCKER_PLATFORM}" ]; then
    docker build --platform "${DOCKER_PLATFORM}" -t "${IMAGE_NAME}" "${ROOT_DIR}"
else
    docker build -t "${IMAGE_NAME}" "${ROOT_DIR}"
fi
