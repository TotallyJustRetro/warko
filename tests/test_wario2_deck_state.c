#include <assert.h>
#include <stdio.h>
#include "wario2_deck_state.h"

int main(void)
{
    uint8_t flags[WARIO2_FLAG_BYTES] = {0};
    flags[0] = 0x81; /* flags 1 and 8 */
    flags[1] = 0x02; /* flag 10 */
    flags[6] = 0x02; /* flag 50 */
    uint64_t mask = wario2_flags_from_bytes(flags);

    assert(wario2_count_flags(mask, 50) == 4);
    assert(wario2_count_flags(mask, 15) == 3);
    assert(wario2_count_flags(UINT64_MAX, 50) == 50);
    assert(wario2_bcd_pair(0x42) == 42);
    assert(wario2_bcd_pair(0xFA) == -1);
    assert(wario2_bcd3(0x00, 0x18, 0x05) == 1805);
    assert(wario2_bcd3(0x99, 0x99, 0x99) == 99999);
    assert(wario2_bcd3(0x10, 0xA0, 0x00) == -1);

    puts("Wario Land II Companion Deck state tests passed.");
    return 0;
}
