# OKVS Notes

This folder contains OKVS code used by PQ-PSI. The current PQ-PSI path uses RB-OKVS.

## RB-OKVS Symbols

| Symbol | Meaning |
| --- | --- |
| `n` | set size per party |
| `eps` | expansion rate |
| `m` | RB-OKVS table width |
| `w` | band width |
| `lambda` | target security parameter |
| `g(eps,n)` | finite-size correction |

Core relations:

```text
m = ceil((1 + eps) n)
lambda ~= 2.751 * eps * w + g(eps,n)
```

## Runtime Knobs

| Knob | Meaning |
| --- | --- |
| `RB_LAMBDA` | target security level, usually `40` |
| `RB_EPS` | table expansion; smaller means less communication but usually larger `w` |
| `RB_COLS` | explicit `m` |
| `RB_W` | explicit band width |

If only `RB_EPS` is set, code computes `m = ceil((1 + eps) n)` and derives `w`.

If `RB_COLS` and `RB_W` are set, code uses them directly.

If all RB knobs are empty, code uses the built-in auto choice.

## Current Defaults

| Size | eps | w | m |
| --- | ---: | ---: | ---: |
| `2^7` | `0.12` | `64` | `144` |
| `2^8` | `0.07` | `88` | `274` |
| `2^9` | `0.07` | `96` | `548` |
| `2^10` | `0.07` | `104` | `1096` |
| `2^11` | `0.07` | `240` | `2192` |

The old `n / 64` width heuristic is no longer used.

## Security Gate

`rbokvs` rejects bad parameter sets.

- `lambda` must be positive
- `w` must stay within the current `256` bit mask limit
- `w` must not exceed `m`
- explicit `eps` and explicit `m` must agree
- explicit `w` must satisfy the current `lambda` target
- current gate uses `lambda = 2.751 eps w + g(eps,n)`

For `2^10` and `2^11`, the `g(eps,n)` gate is paper-guided. Refer to Bienstock et al., *Near-Optimal Oblivious Key-Value Stores for PSI and ZKS*, USENIX Security 2023.

For `2^7` to `2^9`, also check our empirical calibration summary because the paper fit is less trustworthy at these small sizes.

## Empirical `g`

Calibration command:

```bash
bash script/calibrate-rb-g.sh \
  build-docker/benchmarks/rbokvs-pqpsi/rb-g-smart-r100-r1000.md \
  --coarse-rounds 100 \
  --focus-rounds 1000 \
  --lambda 40
```

Method:

- fix `n` and `eps`
- do a coarse scan over candidate `w`
- recheck the coarse boundary window with more rounds
- run standalone `rbokvs_g_check smart`
- test only RB-OKVS encode/decode, not full PSI
- find the smallest focused `w` with zero observed failures
- call it `w_min_pass`
- compute `g_40 = 40 - 2.751 * eps * w_min_pass`
- this is an empirical boundary, not a proof of `2^-40` failure probability

Latest source summary:

```text
build-docker/benchmarks/rbokvs-pqpsi/rb-g-smart-r100-r1000.md
```

Selected rows from the latest run:

| Size | eps | m | w_min_pass | default w |
| --- | ---: | ---: | ---: | ---: |
| `2^7` | `0.12` | `144` | `56` | `64` |
| `2^8` | `0.07` | `274` | `76` | `88` |
| `2^9` | `0.07` | `548` | `80` | `96` |
| `2^10` | `0.07` | `1096` | `84` | `104` |
| `2^11` | `0.07` | `2192` | `-` | `240` |

For `2^7` to `2^10`, the default `w` adds a small margin over the empirical zero-failure boundary. For `2^11, eps=0.07`, the focused empirical window did not find a zero-failure width in `1000` trials. We keep the `2^11` fallback at the paper-guided value `w=240`, where the code's paper fit gives `lambda_real > 40`.

Paper-fit values used for larger sizes:

| Size | eps | g_code |
| --- | ---: | ---: |
| `2^10` | `0.07` | `-5.30` |
| `2^10` | `0.10` | `-6.30` |
| `2^10` | `0.12` | `-6.97` |
| `2^11` | `0.07` | `-6.05` |
| `2^11` | `0.10` | `-7.08` |
| `2^11` | `0.12` | `-7.76` |

## Tuning Workflow

1. Pick one set size.
2. Keep `RB_LAMBDA=40`.
3. Sweep `RB_EPS`.
4. Sweep `RB_W` for each `eps`.
5. Check OKVS-only calibration first.
6. Confirm with full PQ-PSI benchmark.
7. Keep only rows with passing correctness and safe `rb_lambda_real`.
8. Choose the speed winner or communication winner.

Examples:

```bash
SIZES="512" RB_LAMBDA=40 RB_EPS=0.10 RB_W=176 bash script/benchmark-docker-pqpsi.sh
SIZES="256" RB_LAMBDA=40 RB_EPS=0.07 RB_W=208 bash script/benchmark-docker-pqpsi.sh
WARMUPS=1 ROUNDS=5 bash script/tune-rb-pqpsi.sh build-docker/benchmarks/rbokvs-pqpsi/rb-tune-summary.md
```
