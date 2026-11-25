#ifndef REPLACEMENT_H
#define REPLACEMENT_H

#include <stdint.h>
#include "cache.h"

/*
 * Victim selection inside one set. "set" points to the first line of the set
 * and every one of the "ways" lines must be valid (the caller uses an empty
 * line first). Ties are broken toward the lowest way index.
 */
uint32_t replacement_fifo_victim(const CacheLine *set, uint32_t ways);
uint32_t replacement_lru_victim(const CacheLine *set, uint32_t ways);
uint32_t replacement_choose_victim(ReplacementPolicy policy, const CacheLine *set, uint32_t ways);

const char *replacement_name(ReplacementPolicy policy);

#endif
