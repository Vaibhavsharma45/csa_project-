#include "replacement.h"

/* FIFO: the block that was loaded earliest. Hits never change loadedAt. */
uint32_t replacement_fifo_victim(const CacheLine *set, uint32_t ways)
{
    uint32_t victim = 0u;
    for (uint32_t way = 1u; way < ways; way++) {
        if (set[way].loadedAt < set[victim].loadedAt) {
            victim = way;
        }
    }
    return victim;
}

/* LRU: the block whose latest use (hit or load) is the oldest. */
uint32_t replacement_lru_victim(const CacheLine *set, uint32_t ways)
{
    uint32_t victim = 0u;
    for (uint32_t way = 1u; way < ways; way++) {
        if (set[way].lastUsed < set[victim].lastUsed) {
            victim = way;
        }
    }
    return victim;
}

uint32_t replacement_choose_victim(ReplacementPolicy policy, const CacheLine *set, uint32_t ways)
{
    return (policy == POLICY_LRU) ? replacement_lru_victim(set, ways)
                                  : replacement_fifo_victim(set, ways);
}

const char *replacement_name(ReplacementPolicy policy)
{
    switch (policy) {
    case POLICY_FIFO: return "FIFO";
    case POLICY_LRU:  return "LRU";
    default:          return "Unknown";
    }
}
