/*
 * Automated tests for the cache engine, replacement policies and simulator.
 * Links only the logic modules (no raylib). Exit code 0 = all checks passed.
 */
#include <stdio.h>
#include <string.h>

#include "cache.h"
#include "mapping.h"
#include "replacement.h"
#include "simulator.h"

static int g_checks   = 0;
static int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        g_checks++;                                                              \
        if (!(cond)) {                                                           \
            g_failures++;                                                        \
            printf("    FAIL line %d: %s\n", __LINE__, #cond);                   \
        }                                                                        \
    } while (0)

#define CHECK_EQ(actual, expected) CHECK((long long)(actual) == (long long)(expected))

static int near(double a, double b)
{
    const double d = a - b;
    return (d < 0 ? -d : d) < 1e-9;
}

/* ----------------------------------------------------------------- helpers */
static CacheConfig make_cfg(uint32_t cacheSize, uint32_t blockSize, uint32_t memorySize,
                            MappingType mapping, ReplacementPolicy policy, uint32_t ways)
{
    CacheConfig cfg;
    cfg.cacheSize  = cacheSize;
    cfg.blockSize  = blockSize;
    cfg.memorySize = memorySize;
    cfg.ways       = ways;
    cfg.mapping    = mapping;
    cfg.policy     = policy;
    return cfg;
}

static void open_sim(Simulator *sim, uint32_t cacheSize, uint32_t blockSize,
                     MappingType mapping, ReplacementPolicy policy, uint32_t ways)
{
    const CacheConfig cfg = make_cfg(cacheSize, blockSize, 1024u, mapping, policy, ways);
    const ConfigStatus st = simulator_init(sim, &cfg);
    CHECK_EQ(st, CONFIG_OK);
}

/* Performs one access and checks outcome, line and eviction. */
static void expect(Simulator *sim, uint32_t address, AccessOutcome outcome,
                   uint32_t line, int replaced, uint32_t replacedBlock)
{
    AccessResult r;
    const AccessStatus st = simulator_access(sim, address, &r);
    CHECK_EQ(st, ACCESS_OK);
    CHECK_EQ(r.outcome, outcome);
    CHECK_EQ(r.selectedLine, line);
    CHECK_EQ(r.replaced ? 1 : 0, replaced);
    if (replaced) {
        CHECK_EQ(r.replacedBlock, replacedBlock);
    }
}

static void expect_line(const Simulator *sim, uint32_t line, int valid, uint32_t block, uint32_t tag)
{
    const CacheLine *l = &sim->cache.lines[line];
    CHECK_EQ(l->valid ? 1 : 0, valid);
    if (valid) {
        CHECK_EQ(l->blockNumber, block);
        CHECK_EQ(l->tag, tag);
    }
}

/* ------------------------------------------------------------------ tests */
static void test_config_validation(void)
{
    printf("config validation\n");
    CacheConfig c = make_cfg(64, 16, 1024, MAPPING_DIRECT, POLICY_FIFO, 1);
    CHECK_EQ(cache_config_validate(&c), CONFIG_OK);

    c = make_cfg(0, 16, 1024, MAPPING_DIRECT, POLICY_FIFO, 1);
    CHECK_EQ(cache_config_validate(&c), CONFIG_ERR_ZERO_SIZE);
    c = make_cfg(64, 0, 1024, MAPPING_DIRECT, POLICY_FIFO, 1);
    CHECK_EQ(cache_config_validate(&c), CONFIG_ERR_ZERO_SIZE);
    c = make_cfg(64, 16, 0, MAPPING_DIRECT, POLICY_FIFO, 1);
    CHECK_EQ(cache_config_validate(&c), CONFIG_ERR_ZERO_SIZE);
    c = make_cfg(16, 64, 1024, MAPPING_DIRECT, POLICY_FIFO, 1);
    CHECK_EQ(cache_config_validate(&c), CONFIG_ERR_BLOCK_LARGER_THAN_CACHE);
    c = make_cfg(60, 16, 1024, MAPPING_DIRECT, POLICY_FIFO, 1);
    CHECK_EQ(cache_config_validate(&c), CONFIG_ERR_BLOCK_NOT_DIVIDING_CACHE);
    c = make_cfg(16u * 65u, 16, 4096, MAPPING_DIRECT, POLICY_FIFO, 1);
    CHECK_EQ(cache_config_validate(&c), CONFIG_ERR_TOO_MANY_LINES);
    c = make_cfg(64, 16, CACHE_MAX_MEMORY_SIZE + 16u, MAPPING_DIRECT, POLICY_FIFO, 1);
    CHECK_EQ(cache_config_validate(&c), CONFIG_ERR_MEMORY_TOO_LARGE);
    c = make_cfg(64, 16, 32, MAPPING_DIRECT, POLICY_FIFO, 1);
    CHECK_EQ(cache_config_validate(&c), CONFIG_ERR_MEMORY_TOO_SMALL);
    c = make_cfg(64, 16, 1000, MAPPING_DIRECT, POLICY_FIFO, 1);
    CHECK_EQ(cache_config_validate(&c), CONFIG_ERR_MEMORY_NOT_BLOCK_ALIGNED);

    c = make_cfg(64, 16, 1024, MAPPING_SET_ASSOCIATIVE, POLICY_LRU, 2);
    CHECK_EQ(cache_config_validate(&c), CONFIG_OK);
    c.ways = 3;
    CHECK_EQ(cache_config_validate(&c), CONFIG_ERR_BAD_WAYS);
    c.ways = 0;
    CHECK_EQ(cache_config_validate(&c), CONFIG_ERR_BAD_WAYS);
    c.ways = 8;
    CHECK_EQ(cache_config_validate(&c), CONFIG_ERR_BAD_WAYS);

    c = make_cfg(64, 16, 1024, (MappingType)99, POLICY_FIFO, 1);
    CHECK_EQ(cache_config_validate(&c), CONFIG_ERR_BAD_ENUM);
    c = make_cfg(64, 16, 1024, MAPPING_DIRECT, (ReplacementPolicy)99, 1);
    CHECK_EQ(cache_config_validate(&c), CONFIG_ERR_BAD_ENUM);
    CHECK_EQ(cache_config_validate(NULL), CONFIG_ERR_BAD_ENUM);

    /* a rejected configuration must not leave a half-built cache behind */
    Simulator sim;
    c = make_cfg(60, 16, 1024, MAPPING_DIRECT, POLICY_FIFO, 1);
    CHECK_EQ(simulator_init(&sim, &c), CONFIG_ERR_BLOCK_NOT_DIVIDING_CACHE);
    CHECK(sim.cache.lines == NULL);
    CHECK(sim.history == NULL);
}

static void test_address_math(void)
{
    printf("address arithmetic\n");
    Simulator sim;
    AddressParts p;

    open_sim(&sim, 64, 16, MAPPING_DIRECT, POLICY_FIFO, 1);      /* 4 lines */
    cache_decompose(&sim.cache, 100, &p);
    CHECK_EQ(p.blockNumber, 6);  CHECK_EQ(p.offset, 4);
    CHECK_EQ(p.setIndex, 2);     CHECK_EQ(p.tag, 1);             /* 6 % 4, 6 / 4 */
    simulator_destroy(&sim);

    open_sim(&sim, 64, 16, MAPPING_SET_ASSOCIATIVE, POLICY_FIFO, 2);   /* 2 sets */
    cache_decompose(&sim.cache, 100, &p);
    CHECK_EQ(p.setIndex, 0);     CHECK_EQ(p.tag, 3);             /* 6 % 2, 6 / 2 */
    simulator_destroy(&sim);

    open_sim(&sim, 64, 16, MAPPING_FULLY_ASSOCIATIVE, POLICY_FIFO, 1); /* 1 set */
    cache_decompose(&sim.cache, 100, &p);
    CHECK_EQ(p.setIndex, 0);     CHECK_EQ(p.tag, 6);             /* tag = block number */
    simulator_destroy(&sim);
}

static void test_empty_cache_and_ratios(void)
{
    printf("empty cache and zero-division safety\n");
    Simulator sim;
    open_sim(&sim, 64, 16, MAPPING_DIRECT, POLICY_FIFO, 1);
    CHECK_EQ(sim.cache.geometry.totalLines, 4);
    for (uint32_t i = 0; i < 4; i++) {
        CHECK(!sim.cache.lines[i].valid);
    }
    CHECK_EQ(sim.stats.accesses, 0);
    CHECK(near(simulator_hit_ratio(&sim.stats), 0.0));
    CHECK(near(simulator_miss_ratio(&sim.stats), 0.0));
    simulator_destroy(&sim);
}

/* Spec sequence 10,20,10,30,20,40,10 with 16-byte blocks -> blocks 0,1,0,1,1,2,0.
 * Expected tag: direct mapping = block / 4 lines (0 here); fully associative = block. */
static void run_spec_sequence(Simulator *sim, int tagIsBlockNumber)
{
    expect(sim, 10, ACCESS_MISS, 0, 0, 0);   /* first access: compulsory miss */
    expect_line(sim, 0, 1, 0, 0);
    expect(sim, 20, ACCESS_MISS, 1, 0, 0);   /* different block: miss         */
    expect_line(sim, 1, 1, 1, tagIsBlockNumber ? 1 : 0);
    expect(sim, 10, ACCESS_HIT, 0, 0, 0);    /* repeated block: hit           */
    expect(sim, 30, ACCESS_HIT, 1, 0, 0);    /* same block as 20: hit         */
    expect(sim, 20, ACCESS_HIT, 1, 0, 0);
    expect(sim, 40, ACCESS_MISS, 2, 0, 0);
    expect_line(sim, 2, 1, 2, tagIsBlockNumber ? 2 : 0);
    expect(sim, 10, ACCESS_HIT, 0, 0, 0);
    expect_line(sim, 3, 0, 0, 0);            /* untouched line stays empty    */
}

static void check_spec_stats(const Simulator *sim)
{
    CHECK_EQ(sim->stats.accesses, 7);
    CHECK_EQ(sim->stats.hits, 4);
    CHECK_EQ(sim->stats.misses, 3);
    CHECK(near(simulator_hit_ratio(&sim->stats), 400.0 / 7.0));
    CHECK(near(simulator_miss_ratio(&sim->stats), 300.0 / 7.0));
}

static void test_spec_sequence_direct(void)
{
    printf("spec sequence, direct mapping\n");
    Simulator sim;
    open_sim(&sim, 64, 16, MAPPING_DIRECT, POLICY_FIFO, 1);
    run_spec_sequence(&sim, 0);
    check_spec_stats(&sim);
    simulator_destroy(&sim);
}

static void test_spec_sequence_fully_associative(void)
{
    printf("spec sequence, fully associative (lines allocated in order)\n");
    Simulator sim;
    open_sim(&sim, 64, 16, MAPPING_FULLY_ASSOCIATIVE, POLICY_LRU, 1);
    run_spec_sequence(&sim, 1);   /* tags are block numbers */
    check_spec_stats(&sim);
    simulator_destroy(&sim);
}

static void test_direct_collisions(void)
{
    printf("direct mapping collisions\n");
    Simulator sim;
    open_sim(&sim, 64, 16, MAPPING_DIRECT, POLICY_LRU, 1);   /* blocks 0 and 4 share line 0 */
    expect(&sim, 0,  ACCESS_MISS, 0, 0, 0);
    expect(&sim, 64, ACCESS_MISS, 0, 1, 0);                  /* evicts block 0 */
    expect_line(&sim, 0, 1, 4, 1);
    expect(&sim, 0,  ACCESS_MISS, 0, 1, 4);                  /* conflict miss  */
    expect(&sim, 16, ACCESS_MISS, 1, 0, 0);                  /* other lines unaffected */
    simulator_destroy(&sim);
}

static void test_fifo_replacement(void)
{
    printf("FIFO replacement order\n");
    Simulator sim;
    open_sim(&sim, 32, 16, MAPPING_FULLY_ASSOCIATIVE, POLICY_FIFO, 1);   /* 2 lines */
    expect(&sim, 0,  ACCESS_MISS, 0, 0, 0);    /* A */
    expect(&sim, 16, ACCESS_MISS, 1, 0, 0);    /* B */
    expect(&sim, 0,  ACCESS_HIT,  0, 0, 0);    /* hit on A must NOT reorder FIFO */
    expect(&sim, 32, ACCESS_MISS, 0, 1, 0);    /* C evicts A (loaded first) */
    expect(&sim, 0,  ACCESS_MISS, 1, 1, 1);    /* A evicts B (now the oldest) */
    CHECK_EQ(sim.stats.hits, 1);
    CHECK_EQ(sim.stats.misses, 4);
    simulator_destroy(&sim);
}

static void test_lru_replacement(void)
{
    printf("LRU replacement order\n");
    Simulator sim;
    open_sim(&sim, 32, 16, MAPPING_FULLY_ASSOCIATIVE, POLICY_LRU, 1);    /* 2 lines */
    expect(&sim, 0,  ACCESS_MISS, 0, 0, 0);    /* A */
    expect(&sim, 16, ACCESS_MISS, 1, 0, 0);    /* B */
    expect(&sim, 0,  ACCESS_HIT,  0, 0, 0);    /* A becomes most recently used */
    expect(&sim, 32, ACCESS_MISS, 1, 1, 1);    /* C evicts B (least recently used) */
    expect(&sim, 0,  ACCESS_HIT,  0, 0, 0);    /* A survived: FIFO would have missed */
    CHECK_EQ(sim.stats.hits, 2);
    CHECK_EQ(sim.stats.misses, 3);
    simulator_destroy(&sim);
}

static void test_replacement_functions_directly(void)
{
    printf("replacement functions on a hand-built set\n");
    CacheLine set[3];
    memset(set, 0, sizeof set);
    for (uint32_t i = 0; i < 3; i++) {
        set[i].valid = true;
    }
    set[0].loadedAt = 5; set[0].lastUsed = 9;
    set[1].loadedAt = 2; set[1].lastUsed = 8;
    set[2].loadedAt = 7; set[2].lastUsed = 3;
    CHECK_EQ(replacement_fifo_victim(set, 3), 1);   /* smallest loadedAt */
    CHECK_EQ(replacement_lru_victim(set, 3), 2);    /* smallest lastUsed */
    CHECK_EQ(replacement_choose_victim(POLICY_FIFO, set, 3), 1);
    CHECK_EQ(replacement_choose_victim(POLICY_LRU, set, 3), 2);
}

static void test_set_associative(void)
{
    printf("set-associative mapping (4 lines, 2-way -> 2 sets)\n");
    Simulator sim;

    open_sim(&sim, 64, 16, MAPPING_SET_ASSOCIATIVE, POLICY_FIFO, 2);
    CHECK_EQ(sim.cache.geometry.numSets, 2);
    expect(&sim, 0,  ACCESS_MISS, 0, 0, 0);    /* block 0 -> set 0, way 0 */
    expect(&sim, 32, ACCESS_MISS, 1, 0, 0);    /* block 2 -> set 0, way 1 */
    expect(&sim, 16, ACCESS_MISS, 2, 0, 0);    /* block 1 -> set 1        */
    expect(&sim, 64, ACCESS_MISS, 0, 1, 0);    /* block 4 -> set 0 full: FIFO evicts block 0 */
    expect(&sim, 16, ACCESS_HIT,  2, 0, 0);    /* set 1 untouched by set 0 traffic */
    expect(&sim, 0,  ACCESS_MISS, 1, 1, 2);    /* FIFO evicts block 2 (older than block 4) */
    simulator_destroy(&sim);

    open_sim(&sim, 64, 16, MAPPING_SET_ASSOCIATIVE, POLICY_LRU, 2);
    expect(&sim, 0,  ACCESS_MISS, 0, 0, 0);
    expect(&sim, 32, ACCESS_MISS, 1, 0, 0);
    expect(&sim, 0,  ACCESS_HIT,  0, 0, 0);    /* refresh block 0 */
    expect(&sim, 64, ACCESS_MISS, 1, 1, 2);    /* LRU evicts block 2 */
    expect(&sim, 0,  ACCESS_HIT,  0, 0, 0);
    simulator_destroy(&sim);
}

static void test_capacity_boundary(void)
{
    printf("capacity boundary\n");
    Simulator sim;
    open_sim(&sim, 64, 16, MAPPING_FULLY_ASSOCIATIVE, POLICY_FIFO, 1);   /* 4 lines */
    for (uint32_t b = 0; b < 4; b++) {
        expect(&sim, b * 16u, ACCESS_MISS, b, 0, 0);    /* fills without evicting */
    }
    for (uint32_t b = 0; b < 4; b++) {
        expect(&sim, b * 16u, ACCESS_HIT, b, 0, 0);     /* exactly at capacity: all hit */
    }
    expect(&sim, 4u * 16u, ACCESS_MISS, 0, 1, 0);       /* 5th block forces first eviction */
    simulator_destroy(&sim);
}

static void test_invalid_addresses(void)
{
    printf("invalid addresses\n");
    Simulator sim;
    AccessResult r;
    open_sim(&sim, 64, 16, MAPPING_DIRECT, POLICY_FIFO, 1);      /* memory = 1024 bytes */

    CHECK_EQ(simulator_access(&sim, 1024, &r), ACCESS_ERR_ADDRESS_OUT_OF_RANGE);
    CHECK_EQ(simulator_access(&sim, UINT32_MAX, &r), ACCESS_ERR_ADDRESS_OUT_OF_RANGE);
    CHECK_EQ(sim.stats.accesses, 0);
    CHECK_EQ(sim.historyCount, 0);
    CHECK(!sim.cache.lines[0].valid);
    CHECK_EQ(sim.cache.clock, 0);

    CHECK_EQ(simulator_access(&sim, 1023, &r), ACCESS_OK);       /* last valid byte */
    CHECK_EQ(r.blockNumber, 63);
    CHECK_EQ(sim.stats.accesses, 1);
    simulator_destroy(&sim);
}

static void test_reset(void)
{
    printf("reset\n");
    Simulator sim;
    open_sim(&sim, 64, 16, MAPPING_DIRECT, POLICY_FIFO, 1);
    CHECK_EQ(simulator_load_sequence(&sim, "10, 20, 10"), SEQ_OK);
    run_spec_sequence(&sim, 0);
    simulator_reset(&sim);

    CHECK_EQ(sim.stats.accesses, 0);
    CHECK_EQ(sim.stats.hits, 0);
    CHECK_EQ(sim.stats.misses, 0);
    CHECK_EQ(sim.historyCount, 0);
    CHECK_EQ(sim.cache.clock, 0);
    for (uint32_t i = 0; i < 4; i++) {
        CHECK(!sim.cache.lines[i].valid);
    }
    CHECK_EQ(sim.sequenceLength, 3);                    /* the sequence itself survives */
    expect(&sim, 10, ACCESS_MISS, 0, 0, 0);             /* and the first access misses again */
    simulator_destroy(&sim);
}

static void test_undo(void)
{
    printf("previous access (undo by replay)\n");
    Simulator sim;
    AccessResult r;
    open_sim(&sim, 64, 16, MAPPING_DIRECT, POLICY_FIFO, 1);
    CHECK(!simulator_undo(&sim));                       /* nothing to undo */

    simulator_access(&sim, 10, &r);
    simulator_access(&sim, 20, &r);
    simulator_access(&sim, 10, &r);                     /* a hit */
    CHECK_EQ(sim.stats.hits, 1);

    CHECK(simulator_undo(&sim));
    CHECK_EQ(sim.stats.accesses, 2);
    CHECK_EQ(sim.stats.hits, 0);
    CHECK_EQ(sim.stats.misses, 2);
    CHECK_EQ(sim.historyCount, 2);
    expect_line(&sim, 0, 1, 0, 0);
    expect_line(&sim, 1, 1, 1, 0);

    CHECK(simulator_undo(&sim));
    CHECK(simulator_undo(&sim));
    CHECK_EQ(sim.stats.accesses, 0);
    CHECK(!sim.cache.lines[0].valid);
    CHECK(!simulator_undo(&sim));

    /* undoing a conflict eviction restores the evicted block */
    simulator_access(&sim, 0, &r);
    simulator_access(&sim, 64, &r);                     /* evicts block 0 */
    CHECK(simulator_undo(&sim));
    expect_line(&sim, 0, 1, 0, 0);
    simulator_destroy(&sim);
}

static void test_sequences(void)
{
    printf("sequences\n");
    Simulator sim;
    AccessResult r;
    char text[128];
    open_sim(&sim, 64, 16, MAPPING_DIRECT, POLICY_FIFO, 1);

    CHECK_EQ(simulator_load_sequence(&sim, DEFAULT_SEQUENCE_TEXT), SEQ_OK);
    CHECK_EQ(sim.sequenceLength, 7);
    simulator_format_sequence(&sim, text, sizeof text);
    CHECK(strcmp(text, "10, 20, 10, 30, 20, 40, 10") == 0);
    while (simulator_has_next(&sim)) {
        CHECK_EQ(simulator_step_sequence(&sim, &r), ACCESS_OK);
    }
    check_spec_stats(&sim);
    CHECK_EQ(simulator_step_sequence(&sim, &r), ACCESS_ERR_END_OF_SEQUENCE);
    CHECK(simulator_undo(&sim));                        /* undo steps the cursor back */
    CHECK(simulator_has_next(&sim));

    CHECK_EQ(simulator_load_sequence(&sim, ""), SEQ_ERR_EMPTY);
    CHECK_EQ(simulator_load_sequence(&sim, " , ,"), SEQ_ERR_EMPTY);
    CHECK_EQ(simulator_load_sequence(&sim, "10, abc"), SEQ_ERR_SYNTAX);
    CHECK_EQ(simulator_load_sequence(&sim, "-5"), SEQ_ERR_SYNTAX);
    CHECK_EQ(simulator_load_sequence(&sim, "1024"), SEQ_ERR_RANGE);
    CHECK_EQ(simulator_load_sequence(&sim, "99999999999"), SEQ_ERR_RANGE);
    CHECK_EQ(sim.sequenceLength, 7);                    /* failed loads keep the old one */

    char big[SIM_SEQUENCE_CAPACITY * 3 + 16];
    big[0] = '\0';
    for (uint32_t i = 0; i < SIM_SEQUENCE_CAPACITY + 1u; i++) {
        strcat(big, "1,");
    }
    CHECK_EQ(simulator_load_sequence(&sim, big), SEQ_ERR_TOO_LONG);
    simulator_destroy(&sim);

    /* generated sequences are valid and reproducible */
    Simulator a, b;
    open_sim(&a, 64, 16, MAPPING_DIRECT, POLICY_FIFO, 1);
    open_sim(&b, 64, 16, MAPPING_DIRECT, POLICY_FIFO, 1);
    simulator_generate_sequence(&a);
    simulator_generate_sequence(&b);
    CHECK(a.sequenceLength > 0);
    CHECK(memcmp(a.sequence, b.sequence, a.sequenceLength * sizeof a.sequence[0]) == 0);
    for (uint32_t i = 0; i < a.sequenceLength; i++) {
        CHECK(a.sequence[i] < 1024u);
    }
    simulator_destroy(&a);
    simulator_destroy(&b);
}

static void test_explanations(void)
{
    printf("step-by-step explanations\n");
    Simulator sim;
    AccessResult r;
    Explanation ex;
    open_sim(&sim, 64, 16, MAPPING_DIRECT, POLICY_FIFO, 1);

    simulator_access(&sim, 10, &r);
    simulator_explain(&sim, &r, &ex);
    CHECK_EQ(ex.count, 7);
    CHECK(strstr(ex.text[4], "MISS") != NULL);
    CHECK(strstr(r.message, "MISS") != NULL);

    simulator_access(&sim, 10, &r);
    simulator_explain(&sim, &r, &ex);
    CHECK_EQ(ex.count, 7);
    CHECK(strstr(ex.text[4], "HIT") != NULL);

    CHECK_EQ(simulator_access(&sim, 5000, &r), ACCESS_ERR_ADDRESS_OUT_OF_RANGE);
    simulator_explain(&sim, &r, &ex);
    CHECK_EQ(ex.count, 1);
    simulator_destroy(&sim);
}

int main(void)
{
    test_config_validation();
    test_address_math();
    test_empty_cache_and_ratios();
    test_spec_sequence_direct();
    test_spec_sequence_fully_associative();
    test_direct_collisions();
    test_fifo_replacement();
    test_lru_replacement();
    test_replacement_functions_directly();
    test_set_associative();
    test_capacity_boundary();
    test_invalid_addresses();
    test_reset();
    test_undo();
    test_sequences();
    test_explanations();

    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    printf(g_failures == 0 ? "ALL TESTS PASSED\n" : "TESTS FAILED\n");
    return g_failures == 0 ? 0 : 1;
}
