/*
 * Local mlkem-native configuration for the obfuscated ML-KEM tooling.
 *
 * This keeps the integration conservative:
 * - multilevel build so one wrapper can serve 512/768/1024
 * - no SUPERCOP aliases because we need per-level names
 * - no randomized mlkem-native API, since the wrapper supplies derand coins
 * - C backend only for the initial integration
 */

#ifndef OSUCRYPTO_OBF_MLKEM_NATIVE_CONFIG_H
#define OSUCRYPTO_OBF_MLKEM_NATIVE_CONFIG_H

#define MLK_CONFIG_NAMESPACE_PREFIX mlkem
#define MLK_CONFIG_MULTILEVEL_BUILD
#define MLK_CONFIG_NO_RANDOMIZED_API
#define MLK_CONFIG_NO_SUPERCOP

#endif
