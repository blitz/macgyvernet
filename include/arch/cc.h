#pragma once

#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>

typedef uint8_t   u8_t;
typedef uint16_t  u16_t;
typedef uint32_t  u32_t;

typedef int8_t    s8_t;
typedef int16_t   s16_t;
typedef int32_t   s32_t;

typedef uintptr_t mem_ptr_t;

#ifndef BYTE_ORDER
# if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#  define BYTE_ORDER LITTLE_ENDIAN
# elif
#  define BYTE_ORDER BIG_ENDIAN
# else
#  error Endianness unknown
# endif
#endif

#define LWIP_CHKSUM_ALGORITHM 1

#ifdef __cplusplus
# define EXTERN_C extern "C"
#else
# define EXTERN_C
#endif

EXTERN_C void lwip_platform_diag(const char *m, ...);
EXTERN_C void lwip_platform_assert(const char *file, int line, const char *msg);

#define LWIP_PLATFORM_DIAG(msg)   do { lwip_platform_diag   msg; } while (0)
#define LWIP_PLATFORM_ASSERT(msg) do { lwip_platform_assert(__FILE__, __LINE__, msg); } while (0)

#define U16_F PRIu16
#define S16_F PRIi16
#define X16_F PRIx16
#define U32_F PRIu32
#define S32_F PRIi32
#define X32_F PRIx32
#define SZT_F "z"

/* EOF */
