# PQ-PSI

This folder contains the RB-OKVS based PQ-PSI protocol entry and the two party paths.

## Layout

| Path | Purpose |
| --- | --- |
| `pqpsi.cpp` | protocol entry, channel setup, shared run config |
| `protocols/recv.cpp` | receiver path |
| `protocols/send.cpp` | sender path |
| `protocols/tools.h` | KEM, masks, row helpers, threading helpers |
| `../permutation/permutation.h` | top-level permutation choice |
| `../okvs/README.md` | RB-OKVS parameters and tuning notes |

## Protocol

- current path is the optimized RB-OKVS path
- both OKVS rounds use the selected full-key permutation
- second OKVS stores `H(k) xor Perm(v)`
- default permutation is `conspi` with Keccak-f[1600]
- `sneik-f512` selects ConsPi with the NIST LWC SNEIK-f512 permutation
- `hctr` selects AES-128-HCTR2 over the full KEM key
- `ConsPi` uses `lambda = 128` by default

SNEIK note:
the forward SNEIK-f512 code is kept unchanged under `thirdparty/sneik`.
PQ-PSI implements only the inverse adapter in `frontend/permutation/sneik.h`.
That inverse uses byte lookup tables derived from the inverse linear maps, so
it is fast but still follows the same SNEIK-f512 permutation.

## Build

Docker build, recommended on macOS and Linux:

```bash
bash script/build-docker-pqpsi-bench.sh
```

Local Ubuntu 22.04 packages:

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake git \
  libboost-system-dev libboost-thread-dev \
  libgmp-dev libgf2x-dev libntl-dev libsodium-dev
```

Local build:

```bash
bash script/build-miracl-linux64.sh thirdparty/linux/miracl/miracl
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPQPSI_BUILD_RBOKVS_BENCH_ONLY=ON
cmake --build build --target pqpsi_rbokvs_bench rbokvs_g_check rbokvs_pqpsi_test -j"$(nproc)"
```

If you build with Docker on macOS, the binaries are Linux amd64. Run them through the scripts or inside Docker, not directly.

## Quick Test

```bash
bash script/test-rbokvs-pqpsi.sh 256 128
bash script/test-rbokvs-pqpsi.sh 256 128 1 5 43000 --pi keccak800
bash script/test-rbokvs-pqpsi.sh 256 128 1 5 43000 --pi sneik-f512
bash script/test-rbokvs-pqpsi.sh 256 128 1 5 43000 --pi hctr
bash script/test-rbokvs-pqpsi.sh 256 128 1 5 43000 --threads 4
```

The command line test prints receiver and sender timing in the terminal. It does not write markdown.

Useful flags:

| Flag | Meaning |
| --- | --- |
| `--hits <n>` | expected intersection size |
| `--misses <n>` | expected non-intersection count |
| `--rb-eps <v>` | RB-OKVS expansion |
| `--rb-w <v>` | RB-OKVS band width |
| `--rb-cols <v>` | explicit RB-OKVS table width `m` |
| `--rb-lambda <v>` | RB-OKVS security target |
| `--pi <name>` | `keccak1600`, `keccak800`, `sneik-f512`, `conspi`, or `hctr` |
| `--pi-lambda <v>` | `ConsPi` security parameter |
| `--threads <n>` | worker threads per party |
| `--single-thread` | one worker per party |

## Benchmark Summary

Default summary:

```bash
bash script/benchmark-docker-pqpsi.sh
```

Typical explicit summary:

```bash
SIZES="128 256 512 1024 2048" \
WARMUPS=1 ROUNDS=5 DETAIL_MODE=none \
THREADS=4 PI=keccak1600 \
bash script/benchmark-docker-pqpsi.sh build-docker/benchmarks/rbokvs-pqpsi/default-summary.md
```

Compare permutations:

```bash
PI=keccak1600 bash script/benchmark-docker-pqpsi.sh build-docker/benchmarks/rbokvs-pqpsi/conspi-summary.md
PI=hctr       bash script/benchmark-docker-pqpsi.sh build-docker/benchmarks/rbokvs-pqpsi/hctr-summary.md
```

Network examples:

```bash
SIM_DELAY_MS=0  SIM_BW_MBPS=1250 THREADS=1 PI=keccak1600 bash script/benchmark-docker-pqpsi.sh
SIM_DELAY_MS=80 SIM_BW_MBPS=25   THREADS=4 PI=hctr       bash script/benchmark-docker-pqpsi.sh
```

`SIM_DELAY_MS` is one-way delay. `SIM_BW_MBPS=1250` models 10Gbps. `SIM_BW_MBPS=25` models 200Mbps.

## Benchmark Knobs

| Knob | Default | Meaning |
| --- | --- | --- |
| `SIZES` | `128 256 512 1024 2048` | set sizes per party |
| `WARMUPS` | `1` | warmup rounds per size |
| `ROUNDS` | `5` | measured rounds per size |
| `SIM_DELAY_MS` | `0` | one-way simulated delay |
| `SIM_BW_MBPS` | `1250` | simulated bandwidth in MB/s; `1250` models 10Gbps |
| `PI` | `keccak1600` | permutation choice |
| `PI_LAMBDA` | `128` | `ConsPi` lambda |
| `THREADS` | `4` | worker threads per party |
| `DETAIL_MODE` | `none` | `none`, `dir`, or `inline` |

RB-OKVS knobs are documented in `../okvs/README.md`.

## Dataset

The benchmark/test dataset is synthetic.

- a fixed PRNG creates a base set of random `block` items
- both parties start from that same base set
- party `0` rewrites the first `n - hits` items with fresh random items
- expected intersection is exactly `hits`
- default benchmark uses `hits = n - 1`
- command line tests can override this with `--hits` or `--misses`

This applies to `rbRun`, `pqpsi_rbokvs_bench`, and `rbokvs_pqpsi_test`.

## Notes

- Docker is the easiest path for dependency consistency
- use `SIZES="..."` rather than editing scripts for normal sweeps
- use `DETAIL_MODE=none` unless you explicitly need per-size detail reports
