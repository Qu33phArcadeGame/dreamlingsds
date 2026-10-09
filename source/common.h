// Deepdreamlings DS - shared types and helpers
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef PC_BUILD
typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
typedef int64_t s64;
typedef uint64_t u64;
#else
#include <nds/ndstypes.h>   // u8 ... s64 come from libnds on the DS
#endif

#define SCREEN_W 256
#define SCREEN_H 192

// Opaque RGB15 colour (bit 15 = visible on the DS bitmap layer). 0 = transparent.
#define COL(r, g, b) ((u16)(0x8000 | ((r) & 31) | (((g) & 31) << 5) | (((b) & 31) << 10)))
#define COL8(r, g, b) COL((r) >> 3, (g) >> 3, (b) >> 3)
#define C_R(c) ((c) & 31)
#define C_G(c) (((c) >> 5) & 31)
#define C_B(c) (((c) >> 10) & 31)

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
#define IABS(v) ((v) < 0 ? -(v) : (v))

#ifdef PC_BUILD
#define ITCM_FN
#else
// Hot loops run from the ARM9's fast instruction memory, in ARM (not Thumb)
// mode so the compiler can use the 16x16 multiply-accumulate instructions.
#if defined(ITCM_CODE) && defined(ARM_CODE)
#define ITCM_FN ITCM_CODE ARM_CODE
#elif defined(ITCM_CODE)
#define ITCM_FN ITCM_CODE
#else
#define ITCM_FN
#endif
#endif

// tiny random number generator (xorshift32)
u32 rnd(void);
int rnd_range(int n); // 0..n-1
void rnd_seed(u32 s);

// integer sine table, angle 0..1023 = full turn, result -4096..4096
int isin(int a);
int icos(int a);
