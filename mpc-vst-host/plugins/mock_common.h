/* SPDX-License-Identifier: MIT
 * Shared helpers of the mock plug-ins. They are written against this
 * project's own ABI header only; no SDK. */
#ifndef MPCVST_MOCK_COMMON_H
#define MPCVST_MOCK_COMMON_H

#include "mpcvst/vst2_abi.h"

#include <stdlib.h>
#include <string.h>

#define MOCK_EXPORT __attribute__((visibility("default")))

static inline void mock_copy(void *dst, const char *src, size_t cap)
{
    if (!dst)
        return;
    strncpy((char *)dst, src, cap - 1);
    ((char *)dst)[cap - 1] = 0;
}

static inline float mock_clamp01(float v)
{
    if (!(v >= 0.0f))
        return 0.0f;
    return v > 1.0f ? 1.0f : v;
}

#endif
