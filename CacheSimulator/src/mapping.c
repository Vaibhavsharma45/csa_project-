#include "mapping.h"

static uint32_t ways_for(const CacheConfig *cfg, uint32_t lines)
{
    switch (cfg->mapping) {
    case MAPPING_FULLY_ASSOCIATIVE: return lines;
    case MAPPING_SET_ASSOCIATIVE:   return cfg->ways;
    default:                        return 1u;
    }
}

CacheGeometry mapping_geometry(const CacheConfig *cfg)
{
    CacheGeometry g;
    g.totalLines   = cfg->cacheSize / cfg->blockSize;
    g.ways         = ways_for(cfg, g.totalLines);
    if (g.ways == 0u) {
        g.ways = 1u; /* defensive: never divide by zero */
    }
    g.numSets      = g.totalLines / g.ways;
    g.memoryBlocks = cfg->memorySize / cfg->blockSize;
    return g;
}

uint32_t mapping_set_index(const CacheGeometry *g, uint32_t blockNumber)
{
    return blockNumber % g->numSets;
}

uint32_t mapping_tag(const CacheGeometry *g, uint32_t blockNumber)
{
    return blockNumber / g->numSets;
}

uint32_t mapping_first_line(const CacheGeometry *g, uint32_t setIndex)
{
    return setIndex * g->ways;
}

const char *mapping_name(MappingType mapping)
{
    switch (mapping) {
    case MAPPING_DIRECT:            return "Direct";
    case MAPPING_FULLY_ASSOCIATIVE: return "Fully Associative";
    case MAPPING_SET_ASSOCIATIVE:   return "Set Associative";
    default:                        return "Unknown";
    }
}
