#include "simulator.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "replacement.h"

#define RNG_SEED 0x2545F491u

/* ---------------------------------------------------------------- helpers */
static uint32_t next_random(Simulator *sim)
{
    uint32_t x = sim->rngState;   /* xorshift32 */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    sim->rngState = x;
    return x;
}

static void build_message(AccessResult *r)
{
    if (r->outcome == ACCESS_HIT) {
        snprintf(r->message, sizeof r->message,
                 "HIT - block %u found in line %u.",
                 (unsigned)r->blockNumber, (unsigned)r->selectedLine);
    } else if (r->replaced) {
        snprintf(r->message, sizeof r->message,
                 "MISS - block %u replaced block %u in line %u.",
                 (unsigned)r->blockNumber, (unsigned)r->replacedBlock, (unsigned)r->selectedLine);
    } else {
        snprintf(r->message, sizeof r->message,
                 "MISS - block %u loaded into empty line %u.",
                 (unsigned)r->blockNumber, (unsigned)r->selectedLine);
    }
}

/* Shared by manual access, sequence stepping and undo-replay. */
static AccessStatus record_access(Simulator *sim, uint32_t address, bool fromSequence,
                                  AccessResult *out)
{
    if (sim->historyCount >= SIM_HISTORY_CAPACITY) {
        return ACCESS_ERR_HISTORY_FULL;
    }

    AccessResult result;
    const AccessStatus status = cache_access(&sim->cache, address, &result);
    if (status != ACCESS_OK) {
        if (out != NULL) {
            *out = result;
        }
        return status;
    }

    build_message(&result);
    sim->stats.accesses++;
    if (result.outcome == ACCESS_HIT) {
        sim->stats.hits++;
    } else {
        sim->stats.misses++;
    }
    sim->history[sim->historyCount].result       = result;
    sim->history[sim->historyCount].fromSequence = fromSequence;
    sim->historyCount++;

    if (out != NULL) {
        *out = result;
    }
    return ACCESS_OK;
}

/* -------------------------------------------------------------- lifecycle */
ConfigStatus simulator_init(Simulator *sim, const CacheConfig *cfg)
{
    memset(sim, 0, sizeof *sim);

    const ConfigStatus status = cache_init(&sim->cache, cfg);
    if (status != CONFIG_OK) {
        return status;
    }
    sim->history = calloc(SIM_HISTORY_CAPACITY, sizeof *sim->history);
    if (sim->history == NULL) {
        cache_destroy(&sim->cache);
        memset(sim, 0, sizeof *sim);
        return CONFIG_ERR_OUT_OF_MEMORY;
    }
    sim->rngState = RNG_SEED;
    return CONFIG_OK;
}

void simulator_destroy(Simulator *sim)
{
    free(sim->history);
    cache_destroy(&sim->cache);
    memset(sim, 0, sizeof *sim);
}

void simulator_reset(Simulator *sim)
{
    cache_reset(&sim->cache);
    memset(&sim->stats, 0, sizeof sim->stats);
    sim->historyCount = 0u;
    sim->sequenceNext = 0u;
}

/* ----------------------------------------------------------------- access */
AccessStatus simulator_access(Simulator *sim, uint32_t address, AccessResult *result)
{
    if (sim == NULL || sim->history == NULL) {
        return ACCESS_ERR_NULL_ARGUMENT;
    }
    return record_access(sim, address, false, result);
}

bool simulator_undo(Simulator *sim)
{
    if (sim->historyCount == 0u) {
        return false;
    }
    const uint32_t keep = sim->historyCount - 1u;
    if (sim->history[keep].fromSequence && sim->sequenceNext > 0u) {
        sim->sequenceNext--;
    }

    /* The engine is deterministic, so replaying the first "keep" accesses
     * rebuilds the exact cache state. Entry i is read before it is rewritten. */
    cache_reset(&sim->cache);
    memset(&sim->stats, 0, sizeof sim->stats);
    sim->historyCount = 0u;
    for (uint32_t i = 0u; i < keep; i++) {
        record_access(sim, sim->history[i].result.address, sim->history[i].fromSequence, NULL);
    }
    return true;
}

double simulator_hit_ratio(const SimStats *stats)
{
    return (stats->accesses == 0u) ? 0.0
         : 100.0 * (double)stats->hits / (double)stats->accesses;
}

double simulator_miss_ratio(const SimStats *stats)
{
    return (stats->accesses == 0u) ? 0.0
         : 100.0 * (double)stats->misses / (double)stats->accesses;
}

/* -------------------------------------------------------------- sequences */
SequenceStatus simulator_load_sequence(Simulator *sim, const char *text)
{
    uint32_t parsed[SIM_SEQUENCE_CAPACITY];
    uint32_t count = 0u;
    const char *p = (text != NULL) ? text : "";

    while (*p != '\0') {
        if (*p == ' ' || *p == ',' || *p == ';' || *p == '\t' || *p == '\n') {
            p++;
            continue;
        }
        if (*p < '0' || *p > '9') {
            return SEQ_ERR_SYNTAX;
        }
        uint64_t value = 0u;
        while (*p >= '0' && *p <= '9') {
            value = value * 10u + (uint64_t)(*p - '0');
            if (value > UINT32_MAX) {
                return SEQ_ERR_RANGE;
            }
            p++;
        }
        if (value >= sim->cache.config.memorySize) {
            return SEQ_ERR_RANGE;
        }
        if (count >= SIM_SEQUENCE_CAPACITY) {
            return SEQ_ERR_TOO_LONG;
        }
        parsed[count++] = (uint32_t)value;
    }
    if (count == 0u) {
        return SEQ_ERR_EMPTY;
    }

    memcpy(sim->sequence, parsed, count * sizeof parsed[0]);
    sim->sequenceLength = count;
    simulator_reset(sim);
    return SEQ_OK;
}

/* A working set slightly larger than the cache, so replacement really happens. */
void simulator_generate_sequence(Simulator *sim)
{
    enum { POOL_MAX = 12, COUNT = 24 };
    const CacheGeometry *g = &sim->cache.geometry;

    uint32_t poolSize = g->totalLines + 2u;
    if (poolSize > POOL_MAX)        { poolSize = POOL_MAX; }
    if (poolSize > g->memoryBlocks) { poolSize = g->memoryBlocks; }

    uint32_t pool[POOL_MAX];
    for (uint32_t i = 0u; i < poolSize; i++) {
        pool[i] = next_random(sim) % g->memoryBlocks;
    }
    for (uint32_t i = 0u; i < COUNT; i++) {
        const uint32_t block  = pool[next_random(sim) % poolSize];
        const uint32_t offset = next_random(sim) % sim->cache.config.blockSize;
        sim->sequence[i] = block * sim->cache.config.blockSize + offset;
    }
    sim->sequenceLength = COUNT;
    simulator_reset(sim);
}

bool simulator_has_next(const Simulator *sim)
{
    return sim->sequenceNext < sim->sequenceLength;
}

AccessStatus simulator_step_sequence(Simulator *sim, AccessResult *result)
{
    if (!simulator_has_next(sim)) {
        return ACCESS_ERR_END_OF_SEQUENCE;
    }
    const AccessStatus status = record_access(sim, sim->sequence[sim->sequenceNext], true, result);
    if (status == ACCESS_OK) {
        sim->sequenceNext++;
    }
    return status;
}

void simulator_format_sequence(const Simulator *sim, char *buffer, size_t size)
{
    size_t used = 0u;
    if (size == 0u) {
        return;
    }
    buffer[0] = '\0';
    for (uint32_t i = 0u; i < sim->sequenceLength; i++) {
        const int n = snprintf(buffer + used, size - used, "%s%u",
                               (i == 0u) ? "" : ", ", (unsigned)sim->sequence[i]);
        if (n < 0 || (size_t)n >= size - used) {
            buffer[used] = '\0';
            return;
        }
        used += (size_t)n;
    }
}

const char *simulator_sequence_status_message(SequenceStatus status)
{
    switch (status) {
    case SEQ_OK:           return "Sequence loaded.";
    case SEQ_ERR_EMPTY:    return "Sequence is empty. Enter addresses such as 10, 20, 10.";
    case SEQ_ERR_SYNTAX:   return "Sequence may contain only whole numbers separated by commas.";
    case SEQ_ERR_RANGE:    return "A sequence address is outside the memory range.";
    case SEQ_ERR_TOO_LONG: return "Sequence is too long (maximum 128 addresses).";
    default:               return "Invalid sequence.";
    }
}

/* ------------------------------------------------------------ explanation */
#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
static void add_step(Explanation *out, const char *fmt, ...)
{
    if (out->count >= EXPLAIN_MAX_STEPS) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(out->text[out->count], EXPLAIN_LINE_LEN, fmt, args);
    va_end(args);
    out->count++;
}

void simulator_explain(const Simulator *sim, const AccessResult *r, Explanation *out)
{
    const CacheConfig   *cfg = &sim->cache.config;
    const CacheGeometry *g   = &sim->cache.geometry;
    const unsigned first     = (unsigned)mapping_first_line(g, r->setIndex);

    memset(out, 0, sizeof *out);

    if (r->status != ACCESS_OK) {
        add_step(out, "Address %u is outside the memory range 0..%u, so nothing was accessed.",
                 (unsigned)r->address, (unsigned)(cfg->memorySize - 1u));
        return;
    }

    add_step(out, "1. The CPU requests byte address %u.", (unsigned)r->address);
    add_step(out, "2. Block = address / block size = %u / %u = %u; offset %u.",
             (unsigned)r->address, (unsigned)cfg->blockSize,
             (unsigned)r->blockNumber, (unsigned)r->offset);

    switch (cfg->mapping) {
    case MAPPING_DIRECT:
        add_step(out, "3. Direct: line = block %% lines = %u %% %u = %u; tag = %u / %u = %u.",
                 (unsigned)r->blockNumber, (unsigned)g->totalLines, (unsigned)r->setIndex,
                 (unsigned)r->blockNumber, (unsigned)g->totalLines, (unsigned)r->tag);
        break;
    case MAPPING_FULLY_ASSOCIATIVE:
        add_step(out, "3. Fully associative: any of the %u lines; tag = block number = %u.",
                 (unsigned)g->totalLines, (unsigned)r->tag);
        break;
    default:
        add_step(out, "3. %u-way set: set = %u %% %u = %u (lines %u-%u); tag = %u / %u = %u.",
                 (unsigned)g->ways, (unsigned)r->blockNumber, (unsigned)g->numSets, (unsigned)r->setIndex,
                 first, first + (unsigned)g->ways - 1u,
                 (unsigned)r->blockNumber, (unsigned)g->numSets, (unsigned)r->tag);
        break;
    }

    if (r->outcome == ACCESS_HIT) {
        add_step(out, "4. Cached? Yes - line %u is valid and its tag %u matches.",
                 (unsigned)r->selectedLine, (unsigned)r->tag);
        add_step(out, "5. Result: HIT - served from the cache.");
        add_step(out, "6. Line %u stays; its last-used time is refreshed.", (unsigned)r->selectedLine);
        add_step(out, "7. Nothing is replaced.");
        return;
    }

    add_step(out, "4. Cached? No - no valid line in the search area has tag %u.", (unsigned)r->tag);
    add_step(out, "5. Result: MISS - block fetched from main memory.");
    add_step(out, "6. Block %u loaded into line %u (valid = 1, tag = %u).",
             (unsigned)r->blockNumber, (unsigned)r->selectedLine, (unsigned)r->tag);
    if (!r->replaced) {
        add_step(out, "7. Nothing replaced - line %u was empty.", (unsigned)r->selectedLine);
    } else if (g->ways == 1u) {
        add_step(out, "7. Evicted block %u. Only one line is possible, so %s has no choice.",
                 (unsigned)r->replacedBlock, replacement_name(cfg->policy));
    } else {
        add_step(out, "7. Evicted block %u by %s (%s).", (unsigned)r->replacedBlock,
                 replacement_name(cfg->policy),
                 (cfg->policy == POLICY_FIFO) ? "oldest loaded block" : "least recently used block");
    }
}
