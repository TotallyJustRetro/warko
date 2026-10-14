#pragma once
#include <stdint.h>

enum { WARIO2_FLAG_BYTES = 7, WARIO2_COLLECTIBLE_COUNT = 50 };

static inline uint64_t wario2_flags_from_bytes(const uint8_t bytes[WARIO2_FLAG_BYTES])
{
    uint64_t flags = 0;
    if (!bytes) return 0;
    for (int i = 0; i < WARIO2_FLAG_BYTES; ++i)
        flags |= (uint64_t)bytes[i] << (i * 8);
    return flags & ((UINT64_C(1) << WARIO2_COLLECTIBLE_COUNT) - 1);
}

static inline int wario2_count_flags(uint64_t flags, int limit)
{
    int count = 0;
    if (limit < 0) limit = 0;
    if (limit > WARIO2_COLLECTIBLE_COUNT) limit = WARIO2_COLLECTIBLE_COUNT;
    for (int i = 0; i < limit; ++i)
        if (flags & (UINT64_C(1) << i)) ++count;
    return count;
}

static inline int wario2_bcd_pair(uint8_t value)
{
    int tens = (value >> 4) & 0x0F;
    int ones = value & 0x0F;
    return tens <= 9 && ones <= 9 ? tens * 10 + ones : -1;
}

static inline int wario2_bcd3(uint8_t high, uint8_t mid, uint8_t low)
{
    int d0 = high & 0x0F, d1 = (mid >> 4) & 0x0F;
    int d2 = mid & 0x0F, d3 = (low >> 4) & 0x0F, d4 = low & 0x0F;
    if (d0 > 9 || d1 > 9 || d2 > 9 || d3 > 9 || d4 > 9) return -1;
    return d0 * 10000 + d1 * 1000 + d2 * 100 + d3 * 10 + d4;
}
