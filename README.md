# VOLE-PSI with pq-crystals Kyber OT

This is our experimental fork of
[VOLE-PSI](https://github.com/ladnir/volepsi). We use it as a comparison point
for PSI benchmarks where the base OT should be post-quantum, the build should be
reproducible, and the benchmark environment should be explicit rather than
hidden in someone's laptop setup.

The original VOLE-PSI project does the important protocol work. This fork is
mostly the scaffolding we needed around it: a pq-crystals Kyber backend for the
libOTe Kyber OT path, Docker builds, loopback network shaping, and
machine-readable benchmark logs. The goal is not to present a new VOLE-PSI
protocol. The goal is to make the exact variant we benchmarked easy to inspect,
rerun, and criticize.

Upstream VOLE-PSI/libOTe already contains support for a Kyber-based base OT
path. In our macOS Docker Desktop setup, however, that bundled Kyber backend
triggered an illegal-instruction failure when run under the Linux Docker
environment we were using for the comparison benchmarks. Rather than silently
switch back to a non-post-quantum base OT, this fork keeps the libOTe
`ENABLE_MR_KYBER` protocol path and replaces only the Kyber backend with a
pinned pq-crystals Kyber implementation that runs in our Docker environment.

What changed in this fork:

* libOTe is configured with `ENABLE_MR_KYBER=ON`, `ENABLE_MRR=OFF`, and
  `NO_ARCH_NATIVE=ON`.
* The Kyber OT backend is backed by the pq-crystals Kyber reference code,
  pinned to commit `4768bd37c02f9c40a46cb49d4d1f4d5e612bb882`.
* The compatibility layer in `thirdparty/kyberot-pqcrystals/` implements the
  `KyberOT` C API expected by libOTe's `ENABLE_MR_KYBER` code path, while
  delegating Kyber key generation, encapsulation, and decapsulation to the
  pinned pq-crystals Kyber source.
* The TCP benchmark path accepts `-nt <threads>` and prints structured timing
  and byte counters, which makes the scripts less fragile.
* The Docker scripts support both native Linux Docker and macOS Docker Desktop
  running Linux containers.

One measurement detail is worth saying plainly: unless stated otherwise, the
benchmark time reported by these scripts is the online protocol time around
`RsPsiSender::run` / `RsPsiReceiver::run`. It does not include local parameter
initialization, input generation, socket setup, process startup, or Docker
startup. That is intentional, but it should be compared against other protocols
using the same convention.

## Quick Start: Linux Docker

On a Linux host with Docker installed:

```bash
cd VOLE-PSI
bash script/docker-build.sh
bash script/docker-unit-test.sh
bash script/docker-psi-demo.sh 128 64
```

The build script creates the Docker image `vole-psi-linux` by default. Use
`IMAGE_NAME=<name>` if you want to keep multiple builds around.

## Quick Start: macOS with Linux Docker Containers

On macOS, start Docker Desktop first. If you are on Apple Silicon and want to
match a Linux/amd64 comparison environment, set `DOCKER_PLATFORM=linux/amd64`:

```bash
cd VOLE-PSI
DOCKER_PLATFORM=linux/amd64 bash script/docker-build.sh
DOCKER_PLATFORM=linux/amd64 bash script/docker-unit-test.sh
DOCKER_PLATFORM=linux/amd64 bash script/docker-psi-demo.sh 128 64
```

This still runs Linux containers, just through Docker Desktop. On Apple Silicon,
`linux/amd64` uses emulation, so the absolute runtime can differ from a native
Linux/amd64 host. For a paper table, the important rule is consistency: compare
protocols under the same Docker platform and the same network shaping setup.

## Loopback Benchmarks

The main benchmark wrapper starts one Linux container, then runs the sender and
receiver as two frontend processes inside that container. This avoids comparing
one protocol in a single container against another protocol across two separate
containers.

For simulated networks, the script uses Linux `tc netem` on loopback. If you set
`RTT=80ms`, the script applies `40ms` one-way delay, because loopback traffic
sees the delay in both directions.

LAN-style 10 Gbit/s, single-thread:

```bash
cd VOLE-PSI
RATE=10gbit THREAD_MODE=single POWERS="7 8 9 10" WARMUPS=3 ROUNDS=60 \
  bash script/benchmark-docker-loopback-psi.sh results-10gbps-single.md
```

LAN-style 10 Gbit/s, four threads:

```bash
cd VOLE-PSI
RATE=10gbit THREAD_MODE=multi THREADS=4 POWERS="7 8 9 10" WARMUPS=3 ROUNDS=60 \
  bash script/benchmark-docker-loopback-psi.sh results-10gbps-multi4.md
```

WAN-style 200 Mbit/s with 80 ms RTT:

```bash
cd VOLE-PSI
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
* `RTT`: target round-trip delay. The script applies `RTT / 2` as one-way delay.
* `DELAY`: one-way delay. Use either `RTT` or `DELAY`, not both.
* `WARMUPS` / `ROUNDS`: warmup rounds dropped and measured rounds kept.
* `DOCKER_PLATFORM`: optional Docker platform, for example `linux/amd64`.

The report uses `max(sender_time_ms, receiver_time_ms)` as the round time and
`sender bytes_sent + receiver bytes_sent` as total communication.

## About The Original Project

Everything below this point is the upstream VOLE-PSI README, kept here so that
readers can still see the original project description and build notes. Our
fork-specific notes are intentionally above this section.

## Upstream VOLE-PSI README


Vole-PSI implements the protocols described in [VOLE-PSI: Fast OPRF and Circuit-PSI from Vector-OLE](https://eprint.iacr.org/2021/266) and [Blazing Fast PSI from Improved OKVS and Subfield VOLE](misc/blazingFastPSI.pdf). The library implements standard [Private Set Intersection (PSI)](https://en.wikipedia.org/wiki/Private_set_intersection) along with a variant called Circuit PSI where the result is secret shared between the two parties.

The library is cross platform (win,linux,mac) and depends on [libOTe](https://github.com/osu-crypto/libOTe), [sparsehash](https://github.com/sparsehash/sparsehash), [Coproto](https://github.com/Visa-Research/coproto).

### Build

The library can be cloned and built with networking support as
```
git clone https://github.com/Visa-Research/volepsi.git
cd volepsi
python3 build.py -DVOLE_PSI_ENABLE_BOOST=ON
```
If TCP/IP support is not required, then a minimal version of the library can be build by calling `python3 build.py`. See below and the cmake/python output for additional options.
The user can manually call cmake as well.

The output library `volePSI` and executable `frontend` will be written to `out/build/<platform>/`. The `frontend` can perform PSI based on files as input sets and communicate via sockets. See the output of `frontend` for details. There is also two example on how to perform [networking](https://github.com/Visa-Research/volepsi/blob/main/frontend/networkSocketExample.h#L7) or [manually](https://github.com/Visa-Research/volepsi/blob/main/frontend/messagePassingExample.h#L93) get & send the protocol messages.

##### Compile Options
Options can be set as `-D NAME=VALUE`. For example, `-D VOLE_PSI_NO_SYSTEM_PATH=true`. See the output of the build for default/current value. Options include :
 * `VOLE_PSI_NO_SYSTEM_PATH`, values: `true,false`.  When looking for dependencies, do not look in the system install. Instead use `CMAKE_PREFIX_PATH` and the internal dependency management.  
* `CMAKE_BUILD_TYPE`, values: `Debug,Release,RelWithDebInfo`. The build type. 
* `FETCH_AUTO`, values: `true,false`. If true, dependencies will first be searched for and if not found then automatically downloaded.
* `FETCH_SPARSEHASH`, values: `true,false`. If true, the dependency sparsehash will always be downloaded. 
* `FETCH_LIBOTE`, values: `true,false`. If true, the dependency libOTe will always be downloaded. 
* `FETCH_LIBDIVIDE`, values: `true,false`. If true, the dependency libdivide will always be downloaded. 
* `VOLE_PSI_ENABLE_SSE`, values: `true,false`. If true, the library will be built with SSE intrinsics support. 
* `VOLE_PSI_ENABLE_PIC`, values: `true,false`. If true, the library will be built `-fPIC` for shared library support. 
* `VOLE_PSI_ENABLE_ASAN`, values: `true,false`. If true, the library will be built ASAN enabled. 
* `VOLE_PSI_ENABLE_GMW`, values: `true,false`. If true, the GMW protocol will be compiled. Only used for Circuit PSI.
* `VOLE_PSI_ENABLE_CPSI`, values: `true,false`. If true,  the circuit PSI protocol will be compiled. 
* `VOLE_PSI_ENABLE_OPPRF`, values: `true,false`.  If true, the OPPRF protocol will be compiled. Only used for Circuit PSI.
* `VOLE_PSI_ENABLE_BOOST`, values: `true,false`. If true, the library will be built with boost networking support. This support is managed by libOTe. 
* `VOLE_PSI_ENABLE_OPENSSL`, values: `true,false`. If true,the library will be built with OpenSSL networking support. This support is managed by libOTe. If enabled, it is the responsibility of the user to install openssl to the system or to a location contained in `CMAKE_PREFIX_PATH`.
* `VOLE_PSI_ENABLE_BITPOLYMUL`, values: `true,false`. If true, the library will be built with quasicyclic codes for VOLE which are more secure than the alternative. This support is managed by libOTe. 
* `VOLE_PSI_ENABLE_SODIUM`, values: `true,false`. If true, the library will be built libSodium for doing elliptic curve operations. This or relic must be enabled. This support is managed by libOTe. 
* `VOLE_PSI_SODIUM_MONTGOMERY`, values: `true,false`. If true, the library will use a non-standard version of sodium that enables slightly better efficiency. 
* `VOLE_PSI_ENABLE_RELIC`, values: `true,false`. If true, the library will be built relic for doing elliptic curve operations. This or sodium must be enabled. This support is managed by libOTe. 


### Installing

The library and any fetched dependencies can be installed. 
```
python3 build.py --install
```
or 
```
python3 build.py --install=install/prefix/path
```
if a custom install prefix is perfected. Install can also be performed via cmake.

### Linking

libOTe can be linked via cmake as
```
find_package(volepsi REQUIRED)
target_link_libraries(myProject visa::volepsi)
```
To ensure that cmake can find volepsi, you can either install volepsi or build it locally and set `-D CMAKE_PREFIX_PATH=path/to/volepsi` or provide its location as a cmake `HINTS`, i.e. `find_package(volepsi HINTS path/to/volepsi)`.

To link a non-cmake project you will need to link volepsi, libOTe,coproto, macoro, (sodium or relic), optionally boost and openss if enabled. These will be installed to the install location and staged to `./out/install/<platform>`. 


### Dependency Management

By default the dependencies are fetched automatically. This can be turned off by using cmake directly or adding `-D FETCH_AUTO=OFF`. For other options see the cmake output or that of `python build.py --help`.

If the dependency is installed to the system, then cmake should automatically find it if `VOLE_PSI_NO_SYSTEM_PATH` is `false`. If they are installed to a specific location, then you call tell cmake about them as 
```
python3 build.py -D CMAKE_PREFIX_PATH=install/prefix/path
```
