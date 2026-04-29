# VOLE-PSI with pq-crystals Kyber OT

This repository is a benchmarking-oriented fork of
[VOLE-PSI](https://github.com/ladnir/volepsi). It keeps the original VOLE-PSI
protocol code, but adds a Linux Docker workflow, loopback benchmark scripts, and
a pq-crystals Kyber backend for libOTe's `ENABLE_MR_KYBER` base OT path.

The main local changes are:

* libOTe is built with `ENABLE_MR_KYBER=ON`, `ENABLE_MRR=OFF`, and
  `NO_ARCH_NATIVE=ON`.
* The Kyber OT backend is provided by the pq-crystals Kyber reference code,
  pinned to commit `4768bd37c02f9c40a46cb49d4d1f4d5e612bb882`, plus the adapter
  in `thirdparty/kyberot-pqcrystals/`.
* The TCP benchmark frontend accepts `-nt <threads>` in the `-net` path and
  prints machine-readable timing and communication counters.
* Docker scripts are provided for Linux-hosted Docker and macOS Docker Desktop
  running Linux containers.

Unless stated otherwise, reported benchmark time is the online protocol time
around `RsPsiSender::run` / `RsPsiReceiver::run`. It excludes local parameter
initialization, input generation, socket setup, process startup, and Docker
startup.

## Quick Start: Linux Docker

On a Linux host with Docker installed:

```bash
cd VOLE-PSI
bash script/docker-build.sh
bash script/docker-unit-test.sh
bash script/docker-psi-demo.sh 128 64
```

The build script creates the Docker image `vole-psi-linux` by default. Override
it with `IMAGE_NAME=<name>` if needed.

## Quick Start: macOS with Linux Docker Containers

On macOS, start Docker Desktop first. For Apple Silicon machines, use
`linux/amd64` if you want the same Linux/amd64 environment used by our paper
benchmarks:

```bash
cd VOLE-PSI
DOCKER_PLATFORM=linux/amd64 bash script/docker-build.sh
DOCKER_PLATFORM=linux/amd64 bash script/docker-unit-test.sh
DOCKER_PLATFORM=linux/amd64 bash script/docker-psi-demo.sh 128 64
```

This runs Linux containers under Docker Desktop. On Apple Silicon, `linux/amd64`
uses emulation, so absolute timings may differ from a native Linux/amd64 host.
Use the same Docker platform for all compared protocols if the numbers will be
reported together.

## Loopback Benchmarks

The main benchmark wrapper runs sender and receiver as two frontend processes
inside a single Linux container. It can also shape loopback traffic with Linux
`tc netem`; the script applies one-way delay as half of the requested RTT.

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

Useful environment variables:

* `POWERS`: set sizes as log2 values, for example `"7 8 9 10"`.
* `THREAD_MODE`: `single` passes `-nt 1`; `multi` passes `-nt $THREADS`.
* `THREADS`: thread count used when `THREAD_MODE=multi`.
* `RATE`: loopback bandwidth cap passed to `tc netem`, for example `10gbit` or
  `200mbit`.
* `RTT`: target round-trip delay; the script applies `RTT / 2` as one-way delay.
* `DELAY`: one-way delay. Use either `RTT` or `DELAY`, not both.
* `WARMUPS` / `ROUNDS`: warmup rounds dropped and measured rounds kept.
* `DOCKER_PLATFORM`: optional Docker platform, for example `linux/amd64`.

The report uses `max(sender_time_ms, receiver_time_ms)` as the round time and
`sender bytes_sent + receiver bytes_sent` as total communication.

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
