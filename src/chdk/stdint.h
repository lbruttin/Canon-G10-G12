/*
 * stdint.h - for the CHDK build only.
 *
 * CHDK compiles modules with -nostdinc against its own small libc headers,
 * which have no stdint.h. Its modules/flt.h defines the 16- and 32-bit
 * types itself, and soup.c includes both that and the engine's headers, so
 * these have to be the same types flt.h chose or the two disagree inside
 * one file. Taking them from flt.h is the only way to be sure they are.
 */
#ifndef SE_CHDK_STDINT_H
#define SE_CHDK_STDINT_H

#include "flt.h"

typedef unsigned char      uint8_t;
typedef signed char        int8_t;
typedef unsigned long long uint64_t;
typedef long long          int64_t;
typedef unsigned int       uintptr_t;
typedef int                intptr_t;

#endif
