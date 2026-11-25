#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/* ---- Limits (keep the GUI table readable and arithmetic overflow-free) ---- */
#define CACHE_MAX_LINES        64u
#define CACHE_MAX_MEMORY_SIZE  (1u << 20)   /* 1 MiB address space */
#define SIM_HISTORY_CAPACITY   1000u
#define SIM_SEQUENCE_CAPACITY  128u

/* ---- Defaults shown when the application starts ---- */
#define DEFAULT_CACHE_SIZE     64u
#define DEFAULT_BLOCK_SIZE     16u
#define DEFAULT_MEMORY_SIZE    1024u
#define DEFAULT_WAYS           2u
#define DEFAULT_SEQUENCE_TEXT  "10, 20, 10, 30, 20, 40, 10"

typedef enum {
    MAPPING_DIRECT = 0,
    MAPPING_FULLY_ASSOCIATIVE,
    MAPPING_SET_ASSOCIATIVE,
    MAPPING_COUNT
} MappingType;

typedef enum {
    POLICY_FIFO = 0,
    POLICY_LRU,
    POLICY_COUNT
} ReplacementPolicy;

/* All sizes are in bytes. "ways" is used only by set-associative mapping. */
typedef struct {
    uint32_t          cacheSize;
    uint32_t          blockSize;
    uint32_t          memorySize;
    uint32_t          ways;
    MappingType       mapping;
    ReplacementPolicy policy;
} CacheConfig;

#endif
