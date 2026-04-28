#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void sneik_f512(void* state, uint8_t dom, uint8_t rounds);

#ifdef __cplusplus
}
#endif
