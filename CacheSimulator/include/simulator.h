#ifndef SIMULATOR_H
#define SIMULATOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cache.h"

typedef struct {
    uint64_t accesses;
    uint64_t hits;
    uint64_t misses;
} SimStats;

typedef struct {
    AccessResult result;
    bool         fromSequence;   /* true if produced by "Next Access" / auto run */
} HistoryEntry;

typedef struct {
    Cache         cache;
    SimStats      stats;
    HistoryEntry *history;        /* SIM_HISTORY_CAPACITY entries, owned */
    uint32_t      historyCount;
    uint32_t      sequence[SIM_SEQUENCE_CAPACITY];
    uint32_t      sequenceLength;
    uint32_t      sequenceNext;
    uint32_t      rngState;
} Simulator;

typedef enum {
    SEQ_OK = 0,
    SEQ_ERR_EMPTY,
    SEQ_ERR_SYNTAX,
    SEQ_ERR_RANGE,
    SEQ_ERR_TOO_LONG
} SequenceStatus;

/* Lifecycle. simulator_init needs an uninitialised or destroyed Simulator. */
ConfigStatus simulator_init(Simulator *sim, const CacheConfig *cfg);
void         simulator_destroy(Simulator *sim);
void         simulator_reset(Simulator *sim);   /* empty cache, zero stats; keeps the sequence */

/* One CPU memory access. "result" may be NULL. Invalid addresses change nothing. */
AccessStatus simulator_access(Simulator *sim, uint32_t address, AccessResult *result);
bool         simulator_undo(Simulator *sim);    /* previous access: replays history[0..n-2] */

/* Statistics. Both ratios are 0 when there were no accesses. */
double simulator_hit_ratio(const SimStats *stats);    /* percent, 0..100 */
double simulator_miss_ratio(const SimStats *stats);   /* percent, 0..100 */

/* Access sequences. Loading or generating one restarts the simulation. */
SequenceStatus simulator_load_sequence(Simulator *sim, const char *text);
void           simulator_generate_sequence(Simulator *sim);
bool           simulator_has_next(const Simulator *sim);
AccessStatus   simulator_step_sequence(Simulator *sim, AccessResult *result);
void           simulator_format_sequence(const Simulator *sim, char *buffer, size_t size);
const char    *simulator_sequence_status_message(SequenceStatus status);

/* Step-by-step explanation of one access (seven short sentences). */
#define EXPLAIN_MAX_STEPS 7
#define EXPLAIN_LINE_LEN  176

typedef struct {
    size_t count;
    char   text[EXPLAIN_MAX_STEPS][EXPLAIN_LINE_LEN];
} Explanation;

void simulator_explain(const Simulator *sim, const AccessResult *result, Explanation *out);

#endif
