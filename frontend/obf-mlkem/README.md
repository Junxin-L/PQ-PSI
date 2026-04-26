# obf-mlkem

This directory is the `obf-mlkem` tooling under `frontend/obf-mlkem`.

Right now the useful pieces are:

- `backend/`
  `MlKem` is the ML-KEM wrapper. It sits on top of `mlkem-native` and gives us key generation, encapsulation, and decapsulation for ML-KEM-512, ML-KEM-768, and ML-KEM-1024.

- `codec/`
  `Kemeleon` is the encoding layer. It implements the Figure 3 of https://eprint.iacr.org/2024/1086.pdf public-key and ciphertext encoding and decoding logic from the paper.

- `native/`
  This is the local `mlkem-native` integration used by the backend.

## Dependencies

The main dependency here is [thirdparty/mlkem-native](../../../thirdparty/mlkem-native). That is the actual ML-KEM implementation.
**PS: Change files in frontend/obf-mlkem/native if changing the location of mlkem-native**

We also use:

- `libsodium` for randomness and hashing
- `boost::multiprecision` for the big-integer part of the Kemeleon vector codec

Everything else comes from the normal `PQ-PSI` build.

## How to use it

For a plain ML-KEM interface, use `MlKem`.

For Kemeleon, use `Kemeleon` on top of `MlKem`.

The basic flow looks like this:

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

One thing is that `encodeKey(...)` and `encodeCipher(...)` can return `false`, but the code here will not do retry(However it's handled in tests), so callers need to handle that.


## Sizes

The current encoded sizes are:

- ML-KEM-512: key `781`, ciphertext `877`
- ML-KEM-768: key `1156`, ciphertext `1252`
- ML-KEM-1024: key `1530`, ciphertext `1658`

## Tests

The tests for this code are under [UnitTest/obf-mlkem](../../../UnitTest/obf-mlkem).

They cover:

- ML-KEM round trips
- Kemeleon round trips
- malformed input rejection

I built and ran this on an Apple Silicon Mac through the x86_64 path, so the working commands look like this:

```bash
arch -x86_64 /bin/zsh -lc '...'
```

From the repo root, rebuild the test target with:

```bash
arch -x86_64 /bin/zsh -lc 'eval "$(/usr/local/bin/brew shellenv)" && cmake --build build-x86 --target libTests -j1'
```


The `UnitTest/` code builds into the unit-test target. There is not a standalone `obf-mlkem` unit-test binary.


## Benchmark

There is also a small benchmark for the `MlKem` and `Kemeleon` parts.

The source is:

- [UnitTest/obf-mlkem/ObfMlKem_Bench.cpp](../../../UnitTest/obf-mlkem/ObfMlKem_Bench.cpp)

For each mode

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

For `encodeCipher` it also prints a breakdown of:

- average tries per successful encoding
- overflow failures
- zero-rejection failures
- time spent in:
  - unpack
  - pick
  - GMP work
  - reject
  - output copy

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

The benchmark writes a report to:

- [build-x86/obf_mlkem_benchmark.txt](../../../build-x86/obf_mlkem_benchmark.txt)


### Current Report
```
Run Time
  local time            2026-04-12 22:10:01
  system                Darwin 24.3.0 x86_64
  binary arch           x86_64
  build kind            Release-like
  note                  built and run through the x86_64 benchmark path used in this repo

Benchmark Config
  key search tries      128
  cipher warmup tries   32768
  cipher bench tries    4096
  kem rounds            200
  codec rounds          200
  output file           build-x86/obf_mlkem_benchmark.txt


ML-KEM-512
  mode                  ML-KEM-512
  raw pk bytes          800
  raw sk bytes          1632
  raw ct bytes          768
  code pk bytes         781
  code ct bytes         877
  warmup success try    1
  keyGen                      80.41 us
  encaps                      12.95 us
  decaps                      15.96 us
  encodeKey                   34.39 us
  decodeKey                   89.17 us
  encodeCipher                60.44 us
    tries/success  1.06
    overflow fails 0.00
    zero fails     0.07
    unpack         3.21 us
    pick           18.94 us
    mpz            35.25 us
    reject         15.12 us
    output         0.12 us
  decodeCipher                90.50 us

ML-KEM-768
  mode                  ML-KEM-768
  raw pk bytes          1184
  raw sk bytes          2400
  raw ct bytes          1088
  code pk bytes         1156
  code ct bytes         1252
  warmup success try    1
  keyGen                     102.71 us
  encaps                      20.35 us
  decaps                      23.07 us
  encodeKey                   66.44 us
  decodeKey                  180.30 us
  encodeCipher               118.69 us
    tries/success  1.07
    overflow fails 0.00
    zero fails     0.07
    unpack         7.31 us
    pick           35.89 us
    mpz            97.94 us
    reject         22.23 us
    output         0.25 us
  decodeCipher               188.72 us

ML-KEM-1024
  mode                  ML-KEM-1024
  raw pk bytes          1568
  raw sk bytes          3168
  raw ct bytes          1568
  code pk bytes         1530
  code ct bytes         1658
  warmup success try    2
  keyGen                     214.97 us
  encaps                      29.20 us
  decaps                      32.98 us
  encodeKey                  106.71 us
  decodeKey                  317.13 us
  encodeCipher               130.66 us
    tries/success  1.02
    overflow fails 0.00
    zero fails     0.02
    unpack         6.97 us
    pick           30.67 us
    mpz            116.82 us
    reject         3.61 us
    output         0.15 us
  decodeCipher               327.42 us
```
