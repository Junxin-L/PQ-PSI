#!/usr/bin/env bash

set -euo pipefail

DO_INSTALL=0
if [[ "${1:-}" == "--install" ]]; then
    DO_INSTALL=1
fi

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "== PQPSI Linux dependency check =="
    echo
    echo "This helper checks apt packages on native Linux."
    echo "Current host: $(uname -s)"
    echo
    echo "On macOS, use:"
    echo "  bash script/pqpsi.sh build"
    echo "  bash script/pqpsi.sh test process 128 1"
    exit 0
fi

need_cmds=(
    bash
    cmake
    git
    gcc
    g++
    make
)

need_pkgs=(
    build-essential
    cmake
    git
    libboost-system-dev
    libboost-thread-dev
    libgmp-dev
    libsodium-dev
)

miss_cmds=()
miss_pkgs=()

echo "== PQPSI Linux dependency check =="
echo

echo "-- commands --"
for cmd in "${need_cmds[@]}"; do
    if command -v "$cmd" >/dev/null 2>&1; then
        ver="$("$cmd" --version 2>/dev/null | head -n 1 || true)"
        echo "ok   $cmd   ${ver:-version unknown}"
    else
        echo "miss $cmd"
        miss_cmds+=("$cmd")
    fi
done

echo
echo "-- apt packages --"
for pkg in "${need_pkgs[@]}"; do
    if dpkg-query -W -f='${Status} ${Version}\n' "$pkg" 2>/dev/null | grep -q "^install ok installed "; then
        ver="$(dpkg-query -W -f='${Version}\n' "$pkg" 2>/dev/null || true)"
        echo "ok   $pkg   ${ver:-version unknown}"
    else
        echo "miss $pkg"
        miss_pkgs+=("$pkg")
    fi
done

echo
echo "-- extras --"
if [[ -f thirdparty/linux/miracl/miracl/source/miracl.h ]]; then
    echo "ok   miracl source tree"
else
    echo "note miracl source tree not found under thirdparty/linux/miracl/miracl"
fi

if [[ ${#miss_cmds[@]} -eq 0 && ${#miss_pkgs[@]} -eq 0 ]]; then
    echo
    echo "All required Linux dependencies look present"
    exit 0
fi

echo
echo "Missing commands: ${miss_cmds[*]:-none}"
echo "Missing packages: ${miss_pkgs[*]:-none}"

if [[ "$DO_INSTALL" -eq 1 ]]; then
    if [[ ${#miss_pkgs[@]} -eq 0 ]]; then
        echo
        echo "No missing apt packages to install"
        exit 0
    fi

    echo
    echo "Installing missing apt packages"
    sudo apt-get update
    sudo apt-get install -y "${miss_pkgs[@]}"
    echo
    echo "Install done"
else
    echo
    echo "Tip"
    echo "  bash script/check-linux-pqpsi-deps.sh --install"
fi
