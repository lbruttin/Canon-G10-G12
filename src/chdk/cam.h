/*
 * cam.h - for the CHDK build only: just enough of ml/payload/cam.h for
 * ml/payload/libc.c to compile here.
 *
 * Only libc.c's vsnprintf is wanted. CHDK has no vsnprintf to export, and
 * that file's is the one the engine's log lines have always gone through;
 * memcpy and the string functions come from CHDK. The Makefile renames
 * libc.c's own copies of those out of the way, as the host build does, so
 * nothing calls them. Its memcpy is the one place this header is used, and
 * a byte loop keeps even that dead copy correct.
 */
#ifndef SE_CAM_H
#define SE_CAM_H

#include <stdint.h>
#include <stddef.h>

static inline void se_chdk_copy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

#define CAM_memcpy                  0
#define CAM_CALL3(addr, t, a, b, c) se_chdk_copy((a), (b), (size_t)(c))

#endif
