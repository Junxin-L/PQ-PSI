# PQPSI Guide

## Layout

- `pqpsi.cpp`
  protocol entry
- `protocols/recv.cpp`
  receiver path
- `protocols/send.cpp`
  sender path
- `protocols/tools.h`
  KEM and row helpers
- `pi.h`
  Pi construction
- `check.cpp`
  local check entry

## Protocol

- current `pqpsi` is the optimized path
- current `pqpsi` only uses `RB-OKVS`
- second OKVS uses `H(k) xor Pi(v)`
- both OKVS rounds use `Pi`
- `Pi` now uses `lambda = 128`

## Quick Start

Build on Linux Docker

```bash
bash script/check-linux-pqpsi-deps.sh
bash script/build-docker-pqpsi-bench.sh
```

Note

- `script/build-docker-pqpsi-bench.sh` builds Linux amd64 binaries
- on macOS do not run `./bin/pqpsi_rbokvs_bench` or `./bin/rbokvs_pqpsi_test` directly
- on macOS run those binaries inside Docker

Run the default summary

```bash
bash script/benchmark-docker-pqpsi.sh
```

Run the tuned default summary with 10 measured rounds

```bash
WARMUPS=1 ROUNDS=10 bash script/benchmark-docker-pqpsi.sh build-docker/benchmarks/rbokvs-pqpsi/default-summary-10r.md
```

Run the command line test

```bash
bash script/test-rbokvs-pqpsi.sh 256 128

docker run --rm --platform linux/amd64 \
  -v "$PWD:/work" \
  -w /work \
  ubuntu:22.04 \
  bash -lc 'apt-get update >/tmp/apt-update.log 2>&1 && \
    apt-get install -y --no-install-recommends \
      libboost-system1.74.0 libboost-thread1.74.0 \
      libgmp10 libgf2x3 libntl44 libsodium23 >/tmp/apt-install.log 2>&1 && \
    ./bin/rbokvs_pqpsi_test 256 1 5 43000 --hits 128'
```

## Local Linux

Needed tools

- `build-essential`
- `cmake`
- `git`

Needed libs

- `libboost-system-dev`
- `libboost-thread-dev`
- `libgmp-dev`
- `libgf2x-dev`
- `libntl-dev`
- `libsodium-dev`

Install on Ubuntu 22.04

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake git \
  libboost-system-dev libboost-thread-dev \
  libgmp-dev libgf2x-dev libntl-dev libsodium-dev
```

Build `miracl`

This is a repo build dependency from `cryptoTools`
It is not specific to RB-OKVS or Paxos

```bash
bash script/build-miracl-linux64.sh thirdparty/linux/miracl/miracl
```

Configure and build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPQPSI_BUILD_RBOKVS_BENCH_ONLY=ON
cmake --build build --target pqpsi_rbokvs_bench rbokvs_g_check rbokvs_pqpsi_test -j"$(nproc)"
```

## Command Line Test

Command line test only prints to the terminal

```bash
./bin/rbokvs_pqpsi_test <setSize> <warmups> <rounds> <portBase> [delay_ms] [bw_MBps] [rb flags]
```

If you built with `script/build-docker-pqpsi-bench.sh` on macOS

- the binary is Linux amd64
- run it in Docker instead of running it directly

Examples

```bash
bash script/test-rbokvs-pqpsi.sh 256
bash script/test-rbokvs-pqpsi.sh 256 128
bash script/test-rbokvs-pqpsi.sh 256 128 1 5 43000 --rb-eps 0.10 --rb-w 96

docker run --rm --platform linux/amd64 \
  -v "$PWD:/work" \
  -w /work \
  ubuntu:22.04 \
  bash -lc 'apt-get update >/tmp/apt-update.log 2>&1 && \
    apt-get install -y --no-install-recommends \
      libboost-system1.74.0 libboost-thread1.74.0 \
      libgmp10 libgf2x3 libntl44 libsodium23 >/tmp/apt-install.log 2>&1 && \
    ./bin/rbokvs_pqpsi_test 256 1 5 43000 --hits 128'
```

Flags

- `--rb-lambda`
- `--rb-eps`
- `--rb-cols`
- `--rb-w`
- `--hits`
- `--misses`
- `--trace`
- `--show-warmups`
- `--single-thread`
- `--multi-thread`

Output

- each measured round prints one short line
- the final block prints receiver on the left and sender on the right
- no markdown file is written

## Benchmark Binary

Single size only

```bash
./bin/pqpsi_rbokvs_bench [out.md] <setSize> <warmups> <rounds> <portBase> [delay_ms] [bw_MBps] [net_file] [rb flags]
```

Examples

```bash
./bin/pqpsi_rbokvs_bench 512 1 5 43000
./bin/pqpsi_rbokvs_bench 512 1 5 43000 --rb-eps 0.10 --rb-w 160
./bin/pqpsi_rbokvs_bench 512 1 5 43000 --rb-cols 640 --rb-w 64 --rb-lambda 40
./bin/pqpsi_rbokvs_bench 512 1 5 43000 --single-thread
```

## Summary Script

Main entry

```bash
bash script/benchmark-docker-pqpsi.sh
```

Tune one size

```bash
SIZES="1024" RB_EPS=0.10 RB_W=160 bash script/benchmark-docker-pqpsi.sh
```

Tune many sizes

```bash
SIZES="128 256 512 1024 2048" RB_EPS=0.12 bash script/benchmark-docker-pqpsi.sh
```

One file tune summary

```bash
bash script/tune-rb-pqpsi.sh
WARMUPS=1 ROUNDS=5 bash script/tune-rb-pqpsi.sh rb-tune-full.md
WARMUPS=1 ROUNDS=5 bash script/tune-rb-pqpsi.sh build-docker/benchmarks/rbokvs-pqpsi/rb-tune-summary.md
```

Keep temp tune files

```bash
KEEP_WORK=1 bash script/tune-rb-pqpsi.sh
```

Keep detail reports

```bash
DETAIL_MODE=dir bash script/benchmark-docker-pqpsi.sh
```

Only keep the summary

```bash
DETAIL_MODE=none bash script/benchmark-docker-pqpsi.sh
```

## Default Knobs

| Knob | Default | Meaning |
| --- | --- | --- |
| `SIZES` | `128 256 512 1024 2048` | set sizes to sweep |
| `WARMUPS` | `1` | warmup rounds per size |
| `ROUNDS` | `5` | measured rounds per size |
| `SIM_DELAY_MS` | `0` | simulated delay |
| `SIM_BW_MBPS` | `0` | simulated bandwidth |
| `RB_LAMBDA` | `40` | target security parameter |
| `RB_EPS` | empty | target expansion |
| `RB_COLS` | empty | explicit `m` |
| `RB_W` | empty | explicit `w` |
| `THREAD_MODE` | `multi` | `multi` or `single` |
| `DETAIL_MODE` | `none` | `none` `dir` `inline` |

If `RB_EPS` is set

- `m = ceil((1 + eps) n)`
- `w` is derived from `lambda` and realized `eps`

If `RB_COLS` and `RB_W` are set

- `m` and `w` are taken directly

If all `RB_*` knobs are left empty

- default `m` uses a size based `eps`
- `2^7`
  `eps = 0.16`
- `2^8`
  `eps = 0.07`
- `2^9`
  `eps = 0.10`
- `2^10` and up
  `eps = 0.07`
- default `w` is then derived from `lambda`
- the old `n / 64` heuristic is no longer used

Current tuned defaults

| Size | eps | w | m |
| --- | ---: | ---: | ---: |
| `2^7` | `0.16` | `64` | `148` |
| `2^8` | `0.07` | `144` | `274` |
| `2^9` | `0.10` | `96` | `564` |
| `2^10` | `0.07` | `240` | `1096` |
| `2^11` | `0.07` | `240` | `2192` |

## RB Params

Symbols

- `n`
  set size per party
- `eps`
  expansion rate
- `m`
  RB-OKVS table width
- `w`
  band width
- `lambda`
  target security parameter
- `g(eps,n)`
  finite size correction

Core relations

- `m = ceil((1 + eps) n)`
- `lambda ~= 2.751 * eps * w + g(eps,n)`

## Dataset

For the full `pqpsi` benchmark and test path

- the dataset is synthetic
- one base set of random `block` items is generated from a fixed PRNG seed
- both parties start from the same base set
- then party `0` rewrites the first `n - hits` items with fresh random items
- so the expected intersection is exactly `hits`
- benchmark defaults still use `hits = n - 1`
- command line test can override this with `--hits` or `--misses`

This is the path used by

- `rbRun`
- `pqpsi_rbokvs_bench`
- `rbokvs_pqpsi_test`

For the `RB-OKVS` only `g` calibration

- keys and values are also synthetic
- both are generated from a fixed PRNG seed
- the check only tests `RBEncode` plus `RBDecode`
- it does not run the full PSI protocol

What each knob controls

- `RB_LAMBDA`
  target security level
  usually keep this at `40`
- `RB_EPS`
  controls table expansion
  smaller `eps`
  smaller communication
  but usually needs larger `w`
- `RB_COLS`
  explicit `m`
  use this only when you want to set table width directly
- `RB_W`
  explicit band width
  use this when you want to sweep `w` for a fixed size

How the code uses them

If only `RB_EPS` is set

- code computes `m = ceil((1 + eps) n)`
- code then derives `w` from `lambda` and the realized `eps`

If `RB_COLS` and `RB_W` are set

- code uses them directly
- this is the most explicit tuning mode

If all `RB_*` knobs are empty

- code uses the built in auto choice
- this is only a default starter
- do not treat it as the final recommended setting for every size

Which part is paper fit

- for paper fit
  refer to Bienstock et al
  *Near-Optimal Oblivious Key-Value Stores for PSI and ZKS*
  USENIX Security 2023
- in our code
  the `2.751 * eps * w` main term and the conservative `g(eps,n)` approximation are paper guided
- this is the part used for `2^10` and `2^11` by default reasoning

Which part is our own fit

- for `2^7` to `2^9`
  we do not fully trust paper extrapolation
- we also run our own empirical sweeps
- that empirical path checks which `(eps,w)` pairs make `RB-OKVS` itself pass on this implementation
- use that result first for small sizes

How our empirical fit is computed

- we run `script/calibrate-rb-g.sh`
- for each fixed `n` and `eps`
  we sweep a list of candidate `w`
- that script runs the standalone `rbokvs_g_check` binary
- the check uses `lambda=1`
  only to bypass the current width gate during calibration
- each candidate runs an `RB-OKVS` only encode decode test for multiple rounds
- we then look for the smallest tested `w` with `okvs = ok`
- call that `w_min_pass`
- then we compute
  `g_40 = 40 - 2.751 * eps * w_min_pass`
- this gives an empirical `g` approximation for this code path

Current empirical `g` results

- source
  `build-docker/benchmarks/rbokvs-pqpsi/rb-g-summary-20260423-r50.md`

| Size | eps | w_min_pass | g_40 |
| --- | ---: | ---: | ---: |
| `2^7` | `0.14` | `48` | `21.51` |
| `2^7` | `0.15` | `32` | `26.80` |
| `2^7` | `0.16` | `32` | `25.91` |
| `2^7` | `0.17` | `40` | `21.29` |
| `2^7` | `0.18` | `32` | `24.15` |
| `2^7` | `0.19` | `32` | `23.27` |
| `2^7` | `0.20` | `32` | `22.39` |
| `2^8` | `0.07` | `64` | `27.68` |
| `2^8` | `0.08` | `56` | `27.68` |
| `2^8` | `0.09` | `64` | `24.15` |
| `2^8` | `0.10` | `48` | `26.80` |
| `2^8` | `0.11` | `40` | `27.90` |
| `2^8` | `0.12` | `40` | `26.80` |
| `2^9` | `0.07` | `48` | `30.76` |
| `2^9` | `0.08` | `56` | `27.68` |
| `2^9` | `0.09` | `48` | `28.12` |
| `2^9` | `0.10` | `48` | `26.80` |
| `2^9` | `0.11` | `40` | `27.90` |
| `2^9` | `0.12` | `40` | `26.80` |

Paper fit reference we use for larger sizes

| Size | eps | g_code |
| --- | ---: | ---: |
| `2^10` | `0.07` | `-5.30` |
| `2^10` | `0.10` | `-6.30` |
| `2^10` | `0.12` | `-6.97` |
| `2^11` | `0.07` | `-6.05` |
| `2^11` | `0.10` | `-7.08` |
| `2^11` | `0.12` | `-7.76` |

How to read this table

- larger `g_40`
  means the finite size correction is helping more
- smaller `g_40`
  means the main term `2.751 * eps * w` needs to carry more of the target `lambda`
- for `2^7`
  this table is especially coarse
  because the minimum passing `w` hits a very small discrete range

How to choose params in practice

If you want the simplest safe run

- keep `RB_LAMBDA=40`
- set one size at a time
- sweep `RB_EPS`
- for each `RB_EPS`
  sweep `RB_W`
- keep only rows with `okvs = ok`
- then confirm the chosen pair with the full PSI benchmark
- then choose either
  - smallest `runtime_ms`
  - or smallest `comm_kb`

Recommended order

1. pick one `n`
2. keep `RB_LAMBDA=40`
3. pick `RB_EPS`
4. sweep `RB_W`
5. check the OKVS-only calibration summary first
6. confirm with the full PSI benchmark
7. read `rb_lambda_real`
8. keep only safe rows
9. choose speed winner or communication winner

Examples

Single explicit run

```bash
SIZES="512" RB_LAMBDA=40 RB_EPS=0.10 RB_W=176 bash script/benchmark-docker-pqpsi.sh
```

Tune one size by hand

```bash
SIZES="256" RB_LAMBDA=40 RB_EPS=0.07 RB_W=208 bash script/benchmark-docker-pqpsi.sh
SIZES="256" RB_LAMBDA=40 RB_EPS=0.07 RB_W=224 bash script/benchmark-docker-pqpsi.sh
SIZES="256" RB_LAMBDA=40 RB_EPS=0.10 RB_W=176 bash script/benchmark-docker-pqpsi.sh
```

Generate one tuning summary

```bash
WARMUPS=1 ROUNDS=5 bash script/tune-rb-pqpsi.sh build-docker/benchmarks/rbokvs-pqpsi/rb-tune-summary.md
```

Generate empirical `g` for `2^7` to `2^9`

```bash
WARMUPS=1 ROUNDS=5 bash script/calibrate-rb-g.sh build-docker/benchmarks/rbokvs-pqpsi/rb-g-summary.md
```

## Security Checks

`rbokvs` now rejects bad parameter sets

- `lambda` must be positive
- `w` must stay within the current `256` bit mask limit
- `w` must not exceed `m`
- explicit `eps` and explicit `m` must agree
- explicit `w` must still satisfy the current `lambda` target
- current gate uses `lambda = 2.751 eps w + g(eps,n)`
- `g(eps,n)` uses a conservative paper-fit approximation

Important note

- current runtime gate is paper guided
- for `2^10` and `2^11`
  this is the main reference
- for `2^7` to `2^9`
  also check the empirical calibration summary
  do not rely only on paper extrapolation

The benchmark report prints

- `rb_lambda`
- `rb_lambda_real`
- `rb_eps`
- `rb_m`
- `rb_w`

## How To Tune

Recommended workflow

1. sweep one size at a time
2. keep `RB_LAMBDA=40`
3. try paper like `eps` first
4. if `eps` is small then raise `RB_W`
5. pick the fastest config that still passes correctness

Or use the tune script to generate one markdown table for all tested cases

- by default it keeps only the final summary
- failed PSI cases are not used as winners
- for `2^7` to `2^9`
  the `g` fit itself comes from the OKVS-only calibration

Examples

```bash
SIZES="128"  RB_EPS=0.12 RB_W=128 bash script/benchmark-docker-pqpsi.sh
SIZES="256"  RB_EPS=0.10 RB_W=160 bash script/benchmark-docker-pqpsi.sh
SIZES="512"  RB_EPS=0.07 RB_W=208 bash script/benchmark-docker-pqpsi.sh
SIZES="1024" RB_EPS=0.07 RB_W=208 bash script/benchmark-docker-pqpsi.sh
SIZES="2048" RB_EPS=0.07 RB_W=208 bash script/benchmark-docker-pqpsi.sh
```

## Network File

Supported keys in `frontend/benchmarks/network_settings.example.conf`

- `sim_delay_ms`
- `sim_bw_MBps`
- `network_type`
- `network_topology`
- `link_speed`
- `network_note`

Command line `delay_ms` and `bw_MBps` override the file

## Notes

- current benchmark path is centered on Linux x86_64
- Docker is the easiest way to keep dependency versions aligned
- use `SIZES="..."` instead of editing the script when you want a different sweep
