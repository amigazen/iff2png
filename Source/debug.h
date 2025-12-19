/*
** debug.h - Debug output macros
**
** Conditional debug output based on DEBUG define
** Use DEBUG=1 in compiler flags to enable
** Simple printf-based debug output
*/

#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

#ifdef DEBUG
#define DEBUG_PRINTF1(fmt, a1) printf(fmt, a1)
#define DEBUG_PRINTF2(fmt, a1, a2) printf(fmt, a1, a2)
#define DEBUG_PRINTF3(fmt, a1, a2, a3) printf(fmt, a1, a2, a3)
#define DEBUG_PRINTF4(fmt, a1, a2, a3, a4) printf(fmt, a1, a2, a3, a4)
#define DEBUG_PRINTF5(fmt, a1, a2, a3, a4, a5) printf(fmt, a1, a2, a3, a4, a5)
#define DEBUG_PUTSTR(str) printf("%s", str)

#define DEBUG_BYTE_ARRAY(name, data, len) \
    do { \
        LONG _i; \
        printf("DEBUG: %s (%ld bytes): ", name, (long)(len)); \
        for (_i = 0; _i < (len) && _i < 32; _i++) { \
            printf("%02x ", ((UBYTE *)(data))[_i]); \
        } \
        printf("\n"); \
    } while(0)

#define DEBUG_VALUE(name, fmt, val) printf("DEBUG: %s = " fmt "\n", name, val)
#else
#define DEBUG_PRINTF1(fmt, a1) ((void)0)
#define DEBUG_PRINTF2(fmt, a1, a2) ((void)0)
#define DEBUG_PRINTF3(fmt, a1, a2, a3) ((void)0)
#define DEBUG_PRINTF4(fmt, a1, a2, a3, a4) ((void)0)
#define DEBUG_PRINTF5(fmt, a1, a2, a3, a4, a5) ((void)0)
#define DEBUG_PUTSTR(str) ((void)0)
#define DEBUG_BYTE_ARRAY(name, data, len) ((void)0)
#define DEBUG_VALUE(name, fmt, val) ((void)0)
#endif

#endif /* DEBUG_H */

