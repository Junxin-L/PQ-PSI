#define MLK_CONFIG_FILE "obf_mlkem_native_config.h"

#define MLK_CONFIG_MULTILEVEL_WITH_SHARED 1
#define MLK_CONFIG_MONOBUILD_KEEP_SHARED_HEADERS
#define MLK_CONFIG_PARAMETER_SET 512
#include "../../../thirdparty/mlkem-native/mlkem/mlkem_native.c"
#undef MLK_CONFIG_MULTILEVEL_WITH_SHARED
#undef MLK_CONFIG_PARAMETER_SET

#define MLK_CONFIG_MULTILEVEL_NO_SHARED
#define MLK_CONFIG_PARAMETER_SET 768
#include "../../../thirdparty/mlkem-native/mlkem/mlkem_native.c"
#undef MLK_CONFIG_MONOBUILD_KEEP_SHARED_HEADERS
#undef MLK_CONFIG_PARAMETER_SET

#define MLK_CONFIG_PARAMETER_SET 1024
#include "../../../thirdparty/mlkem-native/mlkem/mlkem_native.c"
#undef MLK_CONFIG_PARAMETER_SET
