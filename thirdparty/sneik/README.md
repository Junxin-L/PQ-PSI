SNEIK-f512
==========

This directory contains the portable SNEIK-f512 permutation from the
SNEIKEN/SNEIKHA NIST Lightweight Cryptography Round 1 submission.

Source:
https://csrc.nist.gov/Projects/lightweight-cryptography/round-1-candidates

Files kept here are intentionally minimal:

- `sneik_f512.c`: official portable C99 implementation of `sneik_f512`.
- `sneik_f512.h`: local declaration for the official function.
- `LICENSE`: upstream PQShield research/evaluation license.

PQ-PSI does not modify the upstream forward implementation. The upstream
package only exposes `sneik_f512()`, so PQ-PSI implements the inverse in
`frontend/permutation/sneik.h`.

The inverse keeps the same SNEIK-f512 semantics, but uses byte lookup tables
for the two 32-bit linear inverse maps. The tables are derived from the inverse
matrices at startup; this only changes the adapter speed, not the upstream
permutation.
