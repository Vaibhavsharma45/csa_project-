#include "cache.h"

#include <stdlib.h>
#include <string.h>

#include "replacement.h"

ConfigStatus cache_config_validate(const CacheConfig *cfg)
{
    if (cfg == NULL ||
        (unsigned int)cfg->mapping >= (unsigned int)MAPPING_COUNT ||
        (unsigned int)cfg->policy  >= (unsigned int)POLICY_COUNT) {
        return CONFIG_ERR_BAD_ENUM;
    }
    if (cfg->cacheSize == 0u || cfg->blockSize == 0u || cfg->memorySize == 0u) {
        return CONFIG_ERR_ZERO_SIZE;
    }
    if (cfg->blockSize > cfg->cacheSize) {
        return CONFIG_ERR_BLOCK_LARGER_THAN_CACHE;
    }
    if (cfg->cacheSize % cfg->blockSize != 0u) {
        return CONFIG_ERR_BLOCK_NOT_DIVIDING_CACHE;
    }
    const uint32_t lines = cfg->cacheSize / cfg->blockSize;
    if (lines > CACHE_MAX_LINES) {
        return CONFIG_ERR_TOO_MANY_LINES;
    }
    if (cfg->memorySize > CACHE_MAX_MEMORY_SIZE) {
        return CONFIG_ERR_MEMORY_TOO_LARGE;
    }
    if (cfg->memorySize < cfg->cacheSize) {
        return CONFIG_ERR_MEMORY_TOO_SMALL;
    }
    if (cfg->memorySize % cfg->blockSize != 0u) {
        return CONFIG_ERR_MEMORY_NOT_BLOCK_ALIGNED;
    }
    if (cfg->mapping == MAPPING_SET_ASSOCIATIVE &&
        (cfg->ways == 0u || cfg->ways > lines || lines % cfg->ways != 0u)) {
        return CONFIG_ERR_BAD_WAYS;
    }
    return CONFIG_OK;
}

const char *cache_config_status_message(ConfigStatus status)
{
    switch (status) {
    case CONFIG_OK:                          return "Configuration is valid.";
    case CONFIG_ERR_BAD_ENUM:                return "Unknown mapping or replacement policy.";
    case CONFIG_ERR_ZERO_SIZE:               return "Cache, block and memory sizes must be greater than 0.";
    case CONFIG_ERR_BLOCK_LARGER_THAN_CACHE: return "Block size cannot be larger than the cache size.";
    case CONFIG_ERR_BLOCK_NOT_DIVIDING_CACHE:return "Cache size must be a multiple of the block size.";
    case CONFIG_ERR_TOO_MANY_LINES:          return "Too many cache lines (maximum is 64). Increase the block size.";
    case CONFIG_ERR_MEMORY_TOO_LARGE:        return "Memory size is limited to 1048576 bytes (1 MiB).";
    case CONFIG_ERR_MEMORY_TOO_SMALL:        return "Memory must be at least as large as the cache.";
    case CONFIG_ERR_MEMORY_NOT_BLOCK_ALIGNED:return "Memory size must be a multiple of the block size.";
    case CONFIG_ERR_BAD_WAYS:                return "Ways must divide the number of cache lines.";
    case CONFIG_ERR_OUT_OF_MEMORY:           return "Out of memory.";
    default:                                 return "Invalid configuration.";
    }
}

ConfigStatus cache_init(Cache *cache, const CacheConfig *cfg)
{
    memset(cache, 0, sizeof *cache);

    const ConfigStatus status = cache_config_validate(cfg);
    if (status != CONFIG_OK) {
        return status;
    }

    cache->config   = *cfg;
    cache->geometry = mapping_geometry(cfg);
    cache->lines    = calloc(cache->geometry.totalLines, sizeof *cache->lines);
    if (cache->lines == NULL) {
        memset(cache, 0, sizeof *cache);
        return CONFIG_ERR_OUT_OF_MEMORY;
    }
    return CONFIG_OK;
}

void cache_reset(Cache *cache)
{
    if (cache->lines != NULL) {
        memset(cache->lines, 0, cache->geometry.totalLines * sizeof *cache->lines);
    }
    cache->clock = 0u;
}

void cache_destroy(Cache *cache)
{
    free(cache->lines);
    memset(cache, 0, sizeof *cache);
}

void cache_decompose(const Cache *cache, uint32_t address, AddressParts *parts)
{
    parts->address     = address;
    parts->blockNumber = address / cache->config.blockSize;
    parts->offset      = address % cache->config.blockSize;
    parts->setIndex    = mapping_set_index(&cache->geometry, parts->blockNumber);
    parts->tag         = mapping_tag(&cache->geometry, parts->blockNumber);
}

AccessStatus cache_access(Cache *cache, uint32_t address, AccessResult *result)
{
    if (cache == NULL || cache->lines == NULL || result == NULL) {
        return ACCESS_ERR_NULL_ARGUMENT;
    }

    memset(result, 0, sizeof *result);
    result->address = address;
    if (address >= cache->config.memorySize) {
        result->status = ACCESS_ERR_ADDRESS_OUT_OF_RANGE;
        return result->status;
    }

    AddressParts parts;
    cache_decompose(cache, address, &parts);
    result->blockNumber = parts.blockNumber;
    result->offset      = parts.offset;
    result->setIndex    = parts.setIndex;
    result->tag         = parts.tag;
    result->status      = ACCESS_OK;

    cache->clock++;

    const uint32_t ways  = cache->geometry.ways;
    const uint32_t first = mapping_first_line(&cache->geometry, parts.setIndex);
    CacheLine *set       = &cache->lines[first];

    /* 1. Look only inside the set this block maps to. */
    uint32_t way;
    for (way = 0u; way < ways; way++) {
        if (set[way].valid && set[way].tag == parts.tag) {
            break;
        }
    }
    if (way < ways) {
        set[way].lastUsed    = cache->clock;
        result->outcome      = ACCESS_HIT;
        result->selectedLine = first + way;
        return ACCESS_OK;
    }

    /* 2. Miss: use an empty line if there is one, otherwise evict a victim. */
    result->outcome = ACCESS_MISS;
    for (way = 0u; way < ways; way++) {
        if (!set[way].valid) {
            break;
        }
    }
    if (way == ways) {
        way = replacement_choose_victim(cache->config.policy, set, ways);
        result->replaced       = true;
        result->replacedBlock  = set[way].blockNumber;
    }

    set[way].valid       = true;
    set[way].tag         = parts.tag;
    set[way].blockNumber = parts.blockNumber;
    set[way].loadedAt    = cache->clock;
    set[way].lastUsed    = cache->clock;
    result->selectedLine = first + way;
    return ACCESS_OK;
}

const char *cache_access_status_message(AccessStatus status)
{
    switch (status) {
    case ACCESS_OK:                      return "Access completed.";
    case ACCESS_ERR_NULL_ARGUMENT:       return "Internal error: missing argument.";
    case ACCESS_ERR_ADDRESS_OUT_OF_RANGE:return "Address is outside the configured memory range.";
    case ACCESS_ERR_HISTORY_FULL:        return "History is full (1000 accesses). Press Reset Simulation.";
    case ACCESS_ERR_END_OF_SEQUENCE:     return "End of sequence. Reset the simulation to replay it, or generate a new one.";
    default:                             return "Unknown access error.";
    }
}
