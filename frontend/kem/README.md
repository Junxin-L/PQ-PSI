# KEM choices for PQ-PSI

PQ-PSI currently supports two KEM families. They sit at the same protocol layer:
the selected KEM determines what Alice stores in the first OKVS and what Bob
stores in the second OKVS.

| CLI name | Type | Public-key row | Ciphertext row | Required big permutation |
| --- | --- | ---: | ---: | --- |
| `obf-mlkem` | post-quantum | 800 bytes | 800 bytes | `hctr`, `keccak1600`, `keccak1600-12`, `keccak800`, or `sneik-f512` |
| `eckem` | classical | 48 bytes | 48 bytes | `xoodoo` |

## `obf-mlkem`

`obf-mlkem` means ML-KEM-512 plus the Kemeleon sparse encoding layer.
The implementation lives in [obf-mlkem](obf-mlkem/README.md).

PQ-PSI uses a minimal 800-byte row width for ML-KEM-512:

- ML-KEM-512 raw public key: 800 bytes.
- ML-KEM-512 raw ciphertext plus shared secret: `768 + 32 = 800` bytes.
- Kemeleon is still used to sample sparse public keys, but the protocol row is
  the 800-byte payload consumed by the wide-block permutation.

## `eckem`

`eckem` is the non-post-quantum ECDH+hash sparse KEM candidate. It uses
Monocypher's X25519/Elligator2 support and a 128-bit BLAKE2b tag.
The implementation lives in [eckem](eckem/README.md).

Selecting `--kem eckem` forces the big permutation to `xoodoo`; the other big
permutation choices are intentionally not used with this KEM.

## Tests

The Linux/CMake test binary is `pqpsi_tests`:

```bash
./bin/pqpsi_tests kemeleon
./bin/pqpsi_tests eckem
./bin/pqpsi_tests eckem-bench 128 20
./bin/pqpsi_tests pqpsi-rbokvs 128 1 5 43000 --kem obf-mlkem --pi hctr
./bin/pqpsi_tests pqpsi-rbokvs 128 1 5 43000 --kem eckem --pi xoodoo
```

The Visual Studio `UnitTest/` harness has compatibility smoke tests for the same
KEM components, but the actively maintained command-line path is `pqpsi_tests`.
