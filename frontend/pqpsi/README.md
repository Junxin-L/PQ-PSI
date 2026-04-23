# PQPSI (RB-OKVS) Guide


```bash
cd /path/to/PQ-PSI
```

## 1. Build on macOS (Apple Silicon + x86_64 path)

Use this when your current setup is `build-x86` + Rosetta:

```bash
arch -x86_64 /bin/zsh -lc 'cmake -S . -B build-x86 -DCMAKE_BUILD_TYPE=Release'
arch -x86_64 /bin/zsh -lc 'cmake --build build-x86 -j4 --target pqpsi_rbokvs_bench'
```

## 2. Build on Linux

If your machine is already x86_64 Linux, standard build is enough:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc) --target pqpsi_rbokvs_bench
```

Binary is expected at:

```bash
./bin/pqpsi_rbokvs_bench
```

## 3. Build on Windows (PowerShell + Visual Studio)

Open **x64 Native Tools Command Prompt for VS** or PowerShell with MSVC in PATH:

```powershell
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64
cmake --build build-win --config Release --target pqpsi_rbokvs_bench
```

Then run:

```powershell
.\bin\pqpsi_rbokvs_bench.exe <out.txt> <setSize> <warmups> <rounds> <portBase> [delay_ms] [bw_MBps] [net_settings_file]
```

If `pqpsi_rbokvs_bench.exe` is not under `bin`, search in:

```powershell
.\build-win\**\Release\
```

## 4. Benchmark command format

```bash
./bin/pqpsi_rbokvs_bench <out.txt> <setSize> <warmups> <rounds> <portBase> [delay_ms] [bw_MBps] [net_settings_file]
```

Example: no simulated network limit

```bash
./bin/pqpsi_rbokvs_bench build-x86/benchmarks/pqpsi_rbokvs/pqpsi_rbokvs_benchmark_pretty_nolimit_2p9.txt 512 1 5 43000 0 0
```

Example: with delay and bandwidth simulation

```bash
./bin/pqpsi_rbokvs_bench build-x86/benchmarks/pqpsi_rbokvs/pqpsi_rbokvs_benchmark_sim_2p9.txt 512 1 5 43000 2.0 10.0 frontend/benchmarks/network_settings.example.conf
```

## 5. Network settings file

Supported keys in `frontend/benchmarks/network_settings.example.conf`:

- `sim_delay_ms`
- `sim_bw_MBps`
- `network_type`
- `network_topology`
- `link_speed`
- `network_note`

Command-line `delay_ms` and `bw_MBps` override file values.



