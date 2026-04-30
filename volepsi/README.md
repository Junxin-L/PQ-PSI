# VOLE-PSI with pq-crystals Kyber OT

This README is for running our VOLE-PSI comparison on macOS through Linux
Docker containers. If you are already on Linux, use VOLE-PSI directly instead
of this wrapper.

When used from this monorepo, this code lives under `volepsi/`. Run commands
from that directory.

This is our experimental fork of
[VOLE-PSI](https://github.com/ladnir/volepsi). We use it as a comparison
for PSI benchmarks where the base OT should be post-quantum.

The original VOLE-PSI project does the protocol work. This fork is
mostly the scaffolding we needed around it: a pq-crystals Kyber backend for the
libOTe Kyber OT path, Docker builds, loopback network shaping, and benchmark logs. 

Upstream VOLE-PSI/libOTe already contains support for a Kyber base OT
path. In our macOS Docker Desktop setup, however, that bundled Kyber backend
triggered an illegal-instruction failure when run under the Linux Docker
environment we were using for the comparison benchmarks. This fork keeps the libOTe
`ENABLE_MR_KYBER` protocol path and replaces only the Kyber backend with a pq-crystals Kyber implementation that can run in our Docker environment.

In this fork:

* libOTe is configured with `ENABLE_MR_KYBER=ON`, `ENABLE_MRR=OFF`, and
  `NO_ARCH_NATIVE=ON`.
* The Kyber OT backend is backed by the
  [pq-crystals Kyber reference implementation](https://github.com/pq-crystals/kyber),
  pinned to commit
  [`4768bd37c02f9c40a46cb49d4d1f4d5e612bb882`](https://github.com/pq-crystals/kyber/tree/4768bd37c02f9c40a46cb49d4d1f4d5e612bb882).
* The compatibility layer in `thirdparty/kyberot-pqcrystals/` implements the
  `KyberOT` C API expected by libOTe's `ENABLE_MR_KYBER` code path, while
  delegating Kyber key generation, encapsulation, and decapsulation to pq-crystals Kyber.
* The TCP benchmark path accepts `-nt <threads>` and prints structured timing and byte counters.
* The Docker scripts are for macOS hosts that need a Linux container environment.

Notice: The
benchmark time reported by these scripts is the online protocol time around
`RsPsiSender::run` / `RsPsiReceiver::run`. It does not include local parameter
initialization, input generation, socket setup, process startup, or Docker
startup. 

## Start

On macOS, start Docker Desktop first. If you are on Apple Silicon and want to
match a Linux/amd64 comparison environment, set `DOCKER_PLATFORM=linux/amd64`:

```bash
cd volepsi
DOCKER_PLATFORM=linux/amd64 bash script/docker-build.sh
DOCKER_PLATFORM=linux/amd64 bash script/docker-unit-test.sh
DOCKER_PLATFORM=linux/amd64 bash script/docker-psi-demo.sh 128 64
```

This still runs Linux containers, just through Docker Desktop. On Apple Silicon,
`linux/amd64` uses emulation, so the absolute runtime can differ from a native
Linux/amd64 host. 

The build script creates the Docker image `vole-psi-linux` by default. Use
`IMAGE_NAME=<name>` if you want to keep multiple builds around.

## Loopback Benchmarks

The main benchmark wrapper starts one Linux container, then runs the sender and
receiver as two frontend processes inside that container. 

For simulated networks, the script uses Linux `tc netem` on loopback. 

LAN 10 Gbit/s, single-thread:

```bash
cd volepsi
RATE=10gbit THREAD_MODE=single POWERS="7 8 9 10" WARMUPS=3 ROUNDS=60 \
  bash script/benchmark-docker-loopback-psi.sh results-10gbps-single.md
```

LAN 10 Gbit/s, four threads:

```bash
cd volepsi
RATE=10gbit THREAD_MODE=multi THREADS=4 POWERS="7 8 9 10" WARMUPS=3 ROUNDS=60 \
  bash script/benchmark-docker-loopback-psi.sh results-10gbps-multi4.md
```

WAN 200 Mbit/s with 80 ms RTT:

```bash
cd volepsi
RATE=200mbit RTT=80ms THREAD_MODE=single POWERS="7 8 9 10" WARMUPS=3 ROUNDS=60 \
  bash script/benchmark-docker-loopback-psi.sh results-200mbps-rtt80ms-single.md

RATE=200mbit RTT=80ms THREAD_MODE=multi THREADS=4 POWERS="7 8 9 10" WARMUPS=3 ROUNDS=60 \
  bash script/benchmark-docker-loopback-psi.sh results-200mbps-rtt80ms-multi4.md
```

Useful knobs:

* `POWERS`: set sizes as log2 values, for example `"7 8 9 10"`.
* `THREAD_MODE`: `single` passes `-nt 1`; `multi` passes `-nt $THREADS`.
* `THREADS`: thread count used when `THREAD_MODE=multi`.
* `RATE`: loopback bandwidth cap passed to `tc netem`, for example `10gbit` or
  `200mbit`.
* `RTT`: target round-trip delay. 
* `DELAY`: one-way delay. Use either `RTT` or `DELAY`, not both.
* `DOCKER_PLATFORM`: optional Docker platform, for example `linux/amd64`.

The report uses `max(sender_time_ms, receiver_time_ms)` as the round time and
`sender bytes_sent + receiver bytes_sent` as total communication.

## Linux

On a Linux machine, do not use this macOS Docker wrapper unless you specifically
want to reproduce our container setup. Run VOLE-PSI directly and follow the
VOLE-PSI build instructions instead:

* Upstream VOLE-PSI: <https://github.com/ladnir/volepsi>
* Original fork used here: <https://github.com/Junxin-L/volepsi>
