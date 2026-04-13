# Notes on obf-mlkem

This directory contains the obfuscated ML-KEM tooling built under `libPSI/Tools`.

## What's here

- `backend/`
  `MlKem` is a C++ adapter around `mlkem-native`. It gives keygen, encaps, and decaps for ML-KEM-512/768/1024 with size checks.

- `codec/`
  `Kemeleon` implements the Figure 3 encoding and decoding logic for ML-KEM public keys and ciphertexts.

- `session/`
  `ObfSession` is the current handshake layer on top of `MlKem` and `Kemeleon`. (But we dont need this for pq-psi)

- `native/`
  This is the local `mlkem-native` integration. 

## Main dependencies

The important dependencies for this package are:

- `mlkem-native`
  Vendored under [thirdparty/mlkem-native](.../PQ-PSI/thirdparty/mlkem-native). This is the actual ML-KEM backend.

- `libsodium`
  Used for randomness and hashing in the current implementation.

- `boost::multiprecision`
  Used in `Kemeleon` for the big-integer part of `VectorEncode` / `VectorDecode`.

There are also the normal repo-level dependencies from `PQ-PSI` itself, since this package is built as part of `libPSI`.


## How to use it

- `MlKem`
  Use this if you want ML-KEM keygen, encaps, and decaps.

- `Kemeleon`
  Use this if you want to encode or decode ML-KEM public keys and ciphertexts using the Figure 3 of https://eprint.iacr.org/2024/1086.pdf.

The flow looks like this:

```cpp
using namespace osuCrypto;

MlKem kem(MlKem::Mode::MlKem768);
Kemeleon codec(MlKem::Mode::MlKem768);

auto pair = kem.keyGen();
auto enc = kem.encaps(pair.publicKey);
auto dec = kem.decaps(enc.cipherText, pair.secretKey);

std::vector<u8> keyData;
std::vector<u8> cipherData;

bool ok1 = codec.encodeKey(pair.publicKey, keyData);
bool ok2 = codec.encodeCipher(enc.cipherText, cipherData);
```

Notes:

- `encodeKey(...)` and `encodeCipher(...)` can return `false`

### 2. Use the session wrapper

If you want the current handshake layer, use `ObfSession`.

The flow is:

1. initiator calls `makeHello(...)`
2. responder calls `handleHello(...)`
3. initiator calls `handleReply(...)`
4. both sides read `state().sessionKey`

Example shape:

```cpp
using namespace osuCrypto;

ObfSession alice(MlKem::Mode::MlKem768);
ObfSession bob(MlKem::Mode::MlKem768);

ObfSession::Hello hello;
ObfSession::Reply reply;

bool ok1 = alice.makeHello(hello);
bool ok2 = bob.handleHello(hello.data, reply);
bool ok3 = alice.handleReply(reply.data);

if (ok1 && ok2 && ok3)
{
    auto a = alice.state().sessionKey;
    auto b = bob.state().sessionKey;
}
```


## A note on sizes


Current encoded sizes:

- ML-KEM-512: key `781`, ciphertext `877`
- ML-KEM-768: key `1156`, ciphertext `1252`
- ML-KEM-1024: key `1530`, ciphertext `1658`


## Tests

Tests live under [Tests/obf-mlkem](.../PQ-PSI/Tests/obf-mlkem).

They currently cover:

- ML-KEM round trips
- Kemeleon round trips
- malformed input rejection
- session key agreement across both sides
- some basic session failure cases

### How to build the test targets

(I use Mac

That is why the working commands use:

```bash
arch -x86_64 /bin/zsh -lc '...'
```
)

From the repo root:

```bash
arch -x86_64 /bin/zsh -lc 'eval "$(/usr/local/bin/brew shellenv)" && cmake --build build-x86 --target libTests -j1'
```

That rebuilds the static test library and verifies that the current `obf-mlkem` test files compile cleanly with the rest of the repo.

### Important note about tests in this repo

In the current repo layout, the `Tests/` code builds into `libTests`. There is not a standalone `obf-mlkem` test.


## Benchmark

There is also a small benchmark for the `MlKem` and `Kemeleon` parts.

The benchmark source is:

- [Tests/obf-mlkem/ObfMlKem_Bench.cpp](.../PQ-PSI/Tests/obf-mlkem/ObfMlKem_Bench.cpp)


For each mode:

- `ML-KEM-512`
- `ML-KEM-768`
- `ML-KEM-1024`

it measures:

- `keyGen`
- `encaps`
- `decaps`
- `encodeKey`
- `decodeKey`
- `encodeCipher`
- `decodeCipher`

For `encodeCipher` it also prints a breakdown:

- average tries per successful encoding
- overflow failures
- zero-rejection failures
- time spent in:
  - unpack
  - pick
  - GMP work
  - reject
  - output copy

### Benchmark config

The current benchmark uses:

- key search tries: `128`
- cipher warmup tries: `32768`
- cipher bench tries: `4096`
- KEM rounds: `200`
- codec rounds: `200`


Run:

```bash
arch -x86_64 /bin/zsh -lc 'eval "$(/usr/local/bin/brew shellenv)" && cmake --build build-x86 --target obf_mlkem_bench -j1 && ./bin/obf_mlkem_bench'
```

### Output file

The benchmark writes a report to:

- [build-x86/obf_mlkem_benchmark.txt](.../PQ-PSI/build-x86/obf_mlkem_benchmark.txt)



