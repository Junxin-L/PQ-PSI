# obf-mlkem

`obf-mlkem` is the post-quantum KEM option used by PQ-PSI. It is ML-KEM plus
the Kemeleon sparse encoding layer.

- `backend/`
  `MlKem` is the ML-KEM wrapper. It sits on top of `mlkem-native` and gives us
  key generation, encapsulation, and decapsulation for ML-KEM-512, ML-KEM-768,
  and ML-KEM-1024.

- `codec/`
  `Kemeleon` is the sparse encoding layer. It implements the public-key and
  ciphertext encoding/decoding logic from Figure 3 of
  <https://eprint.iacr.org/2024/1086.pdf>.

- `native/`
  This is the local `mlkem-native` integration used by the backend.

## Dependencies

The main dependency is [thirdparty/mlkem-native](../../../thirdparty/mlkem-native).
That is the actual ML-KEM implementation.

If the location of `mlkem-native` changes, update the files in
`frontend/kem/obf-mlkem/native`.

We also use:

- `libsodium` for randomness and hashing.
- `boost::multiprecision` for the big-integer part of the Kemeleon vector codec.

## Sizes

Kemeleon encoded sizes are:

| Mode | Encoded public key | Encoded ciphertext |
| --- | ---: | ---: |
| ML-KEM-512 | 781 bytes | 877 bytes |
| ML-KEM-768 | 1156 bytes | 1252 bytes |
| ML-KEM-1024 | 1530 bytes | 1658 bytes |

PQ-PSI currently instantiates `obf-mlkem` with ML-KEM-512 and uses an 800-byte
protocol row:

| Payload | Bytes |
| --- | ---: |
| Raw ML-KEM-512 public key | 800 |
| Raw ML-KEM-512 ciphertext | 768 |
| ML-KEM shared secret | 32 |
| PQ-PSI row width | 800 |

The first OKVS stores public-key rows. The second OKVS stores
`ciphertext || shared_secret` rows. Both fit exactly into the same 800-byte row,
so the wide-block permutation operates on 800 bytes for this KEM.

## Randomness

By default, PQ-PSI uses fresh CSPRNG seeds for ML-KEM key generation and
encapsulation. For reproducible benchmark/debug runs, set:

```bash
PQPSI_DETERMINISTIC_KEM=1
```

The deterministic mode is for experiments only; it should not be used as a
security setting.

## Tests

The main command-line tests are in [tests/pqpsi_tests.cpp](../../../tests/pqpsi_tests.cpp):

```bash
./bin/pqpsi_tests kemeleon
./bin/pqpsi_tests pqpsi-rbokvs 128 1 5 43000 --kem obf-mlkem --pi hctr
```

The older Visual Studio-style tests are under
[UnitTest/obf-mlkem](../../../UnitTest/obf-mlkem). They cover:

- ML-KEM round trips.
- Kemeleon round trips.
- malformed input rejection.
- encoded-size checks.

## Benchmark

There is also a small benchmark for the `MlKem` and `Kemeleon` parts:

- [UnitTest/obf-mlkem/ObfMlKem_Bench.cpp](../../../UnitTest/obf-mlkem/ObfMlKem_Bench.cpp)

For each mode it measures:

- `keyGen`
- `encaps`
- `decaps`
- `encodeKey`
- `decodeKey`
- `encodeCipher`
- `decodeCipher`

For `encodeCipher` it also prints a breakdown of:

- average tries per successful encoding.
- overflow failures.
- zero-rejection failures.
- time spent in unpack, pick, GMP work, reject, and output copy.

The current benchmark uses:

- key search tries: `128`
- cipher warmup tries: `32768`
- cipher bench tries: `4096`
- KEM rounds: `200`
- codec rounds: `200`
