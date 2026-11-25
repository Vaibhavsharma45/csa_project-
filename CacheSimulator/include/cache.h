#ifndef CACHE_H
#define CACHE_H

#include <stdbool.h>
#include <stdint.h>
#include "config.h"
#include "mapping.h"

/* ---------------------------------------------------------------- config */
typedef enum {
    CONFIG_OK = 0,
    CONFIG_ERR_BAD_ENUM,
    CONFIG_ERR_ZERO_SIZE,
    CONFIG_ERR_BLOCK_LARGER_THAN_CACHE,
    CONFIG_ERR_BLOCK_NOT_DIVIDING_CACHE,
    CONFIG_ERR_TOO_MANY_LINES,
    CONFIG_ERR_MEMORY_TOO_LARGE,
    CONFIG_ERR_MEMORY_TOO_SMALL,
    CONFIG_ERR_MEMORY_NOT_BLOCK_ALIGNED,
    CONFIG_ERR_BAD_WAYS,
    CONFIG_ERR_OUT_OF_MEMORY
} ConfigStatus;

ConfigStatus cache_config_validate(const CacheConfig *cfg);
const char  *cache_config_status_message(ConfigStatus status);

/* ----------------------------------------------------------------- state */
typedef struct {
    bool     valid;
    uint32_t tag;
    uint32_t blockNumber;
    uint64_t lastUsed;   /* logical time of the latest access (hit or load) */
    uint64_t loadedAt;   /* logical time the block was brought in (FIFO key) */
} CacheLine;

typedef struct {
    CacheConfig   config;
    CacheGeometry geometry;
    CacheLine    *lines;   /* geometry.totalLines entries, owned by the cache */
    uint64_t      clock;   /* logical time, +1 per accepted access */
} Cache;

/* cache_init needs an uninitialised or already-destroyed Cache. */
ConfigStatus cache_init(Cache *cache, const CacheConfig *cfg);
void         cache_reset(Cache *cache);
void         cache_destroy(Cache *cache);

/* --------------------------------------------------------------- address */
typedef struct {
    uint32_t address;
    uint32_t blockNumber;   /* address / blockSize          */
    uint32_t offset;        /* address % blockSize          */
    uint32_t setIndex;      /* blockNumber % numSets        */
    uint32_t tag;           /* blockNumber / numSets        */
} AddressParts;

void cache_decompose(const Cache *cache, uint32_t address, AddressParts *parts);

/* ---------------------------------------------------------------- access */
typedef enum {
    ACCESS_OK = 0,
    ACCESS_ERR_NULL_ARGUMENT,
    ACCESS_ERR_ADDRESS_OUT_OF_RANGE,
    ACCESS_ERR_HISTORY_FULL,        /* reported by the simulator layer */
    ACCESS_ERR_END_OF_SEQUENCE      /* reported by the simulator layer */
} AccessStatus;

typedef enum { ACCESS_HIT = 0, ACCESS_MISS } AccessOutcome;

typedef struct {
    AccessStatus  status;
    AccessOutcome outcome;
    uint32_t      address;
    uint32_t      blockNumber;
    uint32_t      offset;
    uint32_t      setIndex;
    uint32_t      tag;
    uint32_t      selectedLine;    /* absolute line index that was hit or filled */
    bool          replaced;        /* a valid block was evicted */
    uint32_t      replacedBlock;
    char          message[96];     /* one-line summary, filled by the simulator */
} AccessResult;

/* The single reusable access routine used by every mapping and policy. */
AccessStatus cache_access(Cache *cache, uint32_t address, AccessResult *result);
const char  *cache_access_status_message(AccessStatus status);

#endif
