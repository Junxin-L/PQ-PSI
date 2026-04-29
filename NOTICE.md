# Notice

This repository is derived from VOLE-PSI:

* Upstream project: https://github.com/ladnir/volepsi
* Upstream license: MIT, copyright 2022 Visa

The fork keeps the upstream VOLE-PSI license and adds benchmarking-oriented
changes for Linux Docker experiments.

Additional pinned dependencies used by the modified build flow:

* libOTe: https://github.com/osu-crypto/libOTe.git,
  commit `d55867114c78272be7142bd67ebdcb346fec8621`
* pq-crystals Kyber: https://github.com/pq-crystals/kyber.git,
  commit `4768bd37c02f9c40a46cb49d4d1f4d5e612bb882`

The pq-crystals Kyber source is fetched during the build and adapted to the
`KyberOT` interface expected by libOTe's `ENABLE_MR_KYBER` path using the local
adapter in `thirdparty/kyberot-pqcrystals/`.
