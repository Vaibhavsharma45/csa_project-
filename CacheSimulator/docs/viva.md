# Viva Questions and Answers

**1. What is a cache and why is it needed?**
A small, fast memory between the CPU and RAM. The CPU is far faster than main memory, so keeping recently used blocks nearby avoids most slow memory trips.

**2. Why does a cache work at all?**
Locality of reference: temporal (recently used data is used again) and spatial (nearby addresses are used soon). Loading whole blocks exploits spatial locality.

**3. Hit, miss, hit ratio?**
Hit: the block is in the cache. Miss: it is not and must be fetched. Hit ratio = hits / total accesses x 100 (miss ratio = misses / total x 100). Our simulator returns 0 when there are no accesses.

**4. Difference between byte address, block number, offset, line, tag?**
Block = address / block size; offset = address % block size (byte inside the block); line or set = block mod lines or sets; tag = block / sets. The tag identifies *which* block currently sits in a line.

**5. Explain direct mapping.**
Each block maps to exactly one line: line = block % lines. Fast and simple, but two blocks with the same line number keep evicting each other (conflict misses).

**6. Why do we need a tag and a valid bit?**
Many blocks map to the same line, so the tag tells which one is stored. The valid bit says whether the line holds real data at all (empty after reset).

**7. Explain fully associative mapping and its drawback.**
A block may go in any line, so there are no conflict misses, but every line must be searched (many comparators in hardware) and a replacement policy is always needed once full.

**8. What is set-associative mapping?**
A compromise: the cache is divided into sets and a block may use any of the K ways of set (block % sets). K = 1 is direct mapping; K = all lines is fully associative. Our simulator uses this idea for all three.

**9. FIFO versus LRU?**
FIFO evicts the block loaded earliest and ignores hits. LRU evicts the block unused for the longest time, so it keeps recently used blocks and usually has fewer misses, but needs usage tracking on every hit.

**10. How are FIFO and LRU implemented in your code?**
Each line stores `loadedAt` and `lastUsed` from a logical clock that increases per access. FIFO picks the smallest `loadedAt`; LRU picks the smallest `lastUsed`. A hit updates only `lastUsed`.

**11. Does the replacement policy matter in direct mapping?**
No. A block has one possible line, so there is no choice; the GUI disables the policy buttons and the explanation says so.

**12. Types of misses?**
Compulsory (first access), capacity (cache too small for the working set), conflict (blocks compete for the same line or set). Our direct-mapping collision test shows conflict misses.

**13. Can FIFO perform worse with a bigger cache?**
Yes, Belady's anomaly can occur with FIFO. LRU is a stack algorithm and does not suffer from it.

**14. How did you separate GUI and logic?**
Only `gui.c` and `main.c` use raylib. `cache.c`, `mapping.c`, `replacement.c` and `simulator.c` are plain C and are tested by `tests/test_cache.c` without any window.

**15. How does Previous Access work?**
The engine is deterministic, so undo drops the last access and replays the earlier ones from an empty cache, which rebuilds the exact state and statistics.

**16. What does your simulator not model?**
Writes (write-through / write-back), data values, multiple cache levels and timing. These are listed as future scope.

**17. Average memory access time?**
AMAT = hit time + miss rate x miss penalty. Not computed by the simulator, but the hit and miss ratios it shows are the inputs.
