# Notice

This repository started as a fork of VOLE-PSI:

* Upstream project: https://github.com/ladnir/volepsi
* Upstream license: MIT
* Upstream copyright: 2022 Visa

We are grateful to the VOLE-PSI authors for releasing their implementation. Our
changes are aimed at benchmarking and reproducibility: Dockerized Linux builds,
scripted loopback experiments, structured benchmark output, and a pq-crystals
Kyber backend for the libOTe Kyber OT path.

The modified build flow pins the relevant third-party code:

* libOTe: https://github.com/osu-crypto/libOTe.git,
  commit `d55867114c78272be7142bd67ebdcb346fec8621`
* pq-crystals Kyber: https://github.com/pq-crystals/kyber.git,
  commit `4768bd37c02f9c40a46cb49d4d1f4d5e612bb882`

The pq-crystals Kyber source is fetched at build time. The compatibility layer
in `thirdparty/kyberot-pqcrystals/` implements the `KyberOT` C API expected by
libOTe's `ENABLE_MR_KYBER` path and calls the pinned pq-crystals Kyber routines
for key generation, encapsulation, and decapsulation.

This fork is a modified benchmark artifact, not the original
VOLE-PSI implementation and not a new VOLE-PSI protocol.
