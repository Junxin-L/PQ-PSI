# PQPSI (RB-OKVS) Quick Guide

This folder contains the PQPSI flow code used by the benchmark binary:

- `pqpsi.cpp`
- `pqpsi.h`
- `permutation.h`

## Build

From repo root:

```bash
arch -x86_64 /bin/zsh -lc 'cmake --build build-x86 -j4 --target pqpsi_rbokvs_bench'
```

## Run Benchmark

From repo root:

```bash
./bin/pqpsi_rbokvs_bench <out.txt> <setSize> <warmups> <rounds> <portBase> [delay_ms] [bw_MBps] [net_settings_file]
```

Example (no simulated network limit):

```bash
./bin/pqpsi_rbokvs_bench build-x86/benchmarks/pqpsi_rbokvs/pqpsi_rbokvs_benchmark_pretty_nolimit_2p9.txt 512 1 5 43000 0 0
```

Example (with simulated network delay/bandwidth):

```bash
./bin/pqpsi_rbokvs_bench build-x86/benchmarks/pqpsi_rbokvs/pqpsi_rbokvs_benchmark_sim_2p9.txt 512 1 5 43000 2.0 10.0 frontend/benchmarks/network_settings.example.conf
```

## Network Settings File

Supported keys in `network_settings.example.conf`:

- `sim_delay_ms`
- `sim_bw_MBps`
- `network_type`
- `network_topology`
- `link_speed`
- `network_note`

Command-line `delay_ms` / `bw_MBps` override file values.

## Interpreting Report Fields

- `keygen`: ML-KEM keygen + Kemeleon key encoding stage
- `permute`: forward permutation stage
- `permute_inverse`: inverse permutation stage before KEM operations
- `kem_ops (Encaps/Decaps)`: core encaps/decaps stage
- `network_send`: socket send time (+ simulated network wait if enabled)
- `network_recv`: blocking recv wait time (often includes waiting for peer compute)

## Test/Run Stability Tips

- Use a fresh `portBase` for each run to avoid port reuse conflicts.
- If multi-round mode behaves unstable on your machine, run multiple 1-round jobs with different ports and aggregate.
- Keep `PQPSI_TRACE=0` for performance runs.
