#!/usr/bin/env bash

set -euo pipefail

IMAGE_NAME="${IMAGE_NAME:-vole-psi-linux}"
DOCKER_PLATFORM="${DOCKER_PLATFORM:-}"

if [ "$#" -eq 0 ]; then
    set -- -u
fi

if [ -n "${DOCKER_PLATFORM}" ]; then
    docker run --rm --platform "${DOCKER_PLATFORM}" "${IMAGE_NAME}" ./out/build/linux/frontend/frontend "$@"
else
    docker run --rm "${IMAGE_NAME}" ./out/build/linux/frontend/frontend "$@"
fi
