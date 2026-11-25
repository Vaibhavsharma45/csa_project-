#ifndef MAPPING_H
#define MAPPING_H

#include <stdint.h>
#include "config.h"

/*
 * All three mappings are the same machine with different shapes:
 *   direct            -> numSets = lines, ways = 1
 *   fully associative -> numSets = 1,     ways = lines
 *   set associative   -> numSets = lines / ways
 * A block may live only in the "ways" lines of set (block % numSets).
 */
typedef struct {
    uint32_t totalLines;
    uint32_t ways;
    uint32_t numSets;
    uint32_t memoryBlocks;
} CacheGeometry;

/* The functions below assume a configuration that passed cache_config_validate(). */
CacheGeometry mapping_geometry(const CacheConfig *cfg);

uint32_t mapping_set_index(const CacheGeometry *g, uint32_t blockNumber);
uint32_t mapping_tag(const CacheGeometry *g, uint32_t blockNumber);
uint32_t mapping_first_line(const CacheGeometry *g, uint32_t setIndex);

const char *mapping_name(MappingType mapping);

#endif
