#ifndef OSUCRYPTO_MLKEM_NATIVE_ALL_H
#define OSUCRYPTO_MLKEM_NATIVE_ALL_H

#define MLK_CONFIG_FILE "obf_mlkem_native_config.h"

#define MLK_CONFIG_PARAMETER_SET 512
#include "../../../thirdparty/mlkem-native/mlkem/mlkem_native.h"
#undef MLK_CONFIG_PARAMETER_SET
#undef MLK_H

#define MLK_CONFIG_PARAMETER_SET 768
#include "../../../thirdparty/mlkem-native/mlkem/mlkem_native.h"
#undef MLK_CONFIG_PARAMETER_SET
#undef MLK_H

#define MLK_CONFIG_PARAMETER_SET 1024
#include "../../../thirdparty/mlkem-native/mlkem/mlkem_native.h"
#undef MLK_CONFIG_PARAMETER_SET
#undef MLK_H

#endif
