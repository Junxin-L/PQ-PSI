# PQ-PSI Internals

Protocol wiring only. Build, test, benchmark, Docker, and citations are in the
root `README.md`.

## Layout

| Path | Notes |
| --- | --- |
| `pqpsi.cpp` | entry point and set setup |
| `pqpsi.h` | configs and timing profile |
| `protocols/party.h` | shared party state |
| `protocols/recv.cpp` | Alice |
| `protocols/send.cpp` | Bob |
| `protocols/net.h` | channel helpers |
| `protocols/tools/` | rows, masks, parallel helpers |
| `../okvs/` | RB-OKVS |
| `../kem/` | KEM rows |
| `../permutation/` | big permutations |

## Roles

Party `0` is Alice/receiver with set `X`.
Party `1` is Bob/sender with set `Y`.

KEM rows and permutation choices are in `../kem/README.md` and the root README.
