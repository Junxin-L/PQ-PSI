# eckem

`eckem` is the classical, non-post-quantum sparse KEM candidate for PQ-PSI.

It uses Monocypher's X25519 + Elligator2 implementation:

| item       | bytes |
| ---------- | ----: |
| public key |    32 |
| secret key |    32 |
| ciphertext |    48 |
| tag        |    16 |

The public key is an Elligator representative of `pk = g^sk`.
The ciphertext is:

```text
rep(R = g^r) || H(pk^r)
```

where `H` is a BLAKE2b-based 128-bit KDF. Decapsulation maps `rep(R)` back to an
X25519 public key, computes `R^sk`, and returns `bot` unless the 128-bit tag
matches.

This KEM is not post-quantum. In PQ-PSI it is paired only with the Xoodoo big
permutation path.

PQ-PSI wiring:

| row use        | bytes | layout                                               |
| -------------- | ----: | ---------------------------------------------------- |
| public-key row |    48 | `rep(pk)` in the first 32 bytes, then 16 zero bytes  |
| ciphertext row |    48 | `rep(R)` in the first 32 bytes, then the 16-byte tag |
| permutation    |    48 | exactly one Xoodoo state                             |

Selecting `--kem eckem` in PQ-PSI forces the big permutation to `xoodoo`.

## Tests

```bash
./bin/pqpsi_tests eckem
./bin/pqpsi_tests eckem-bench 128 20
./bin/pqpsi_tests pqpsi-rbokvs 128 1 5 43000 --kem eckem --pi xoodoo
```

The `eckem` test checks deterministic key generation, encapsulation,
decapsulation, wrong-key rejection, corrupted-ciphertext rejection, and random
ciphertext sparsity. The `eckem-bench` command reports separate keygen, encap,
and decap timings.
