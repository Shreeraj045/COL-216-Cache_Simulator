#include "L1Cache.h"

L1Cache::L1Cache(int id, int s_bits, int b_bits, int assoc)
    : core_id(id), S(1 << s_bits), E(assoc), B(1 << b_bits), s(s_bits), b(b_bits)
{
    cache_sets.resize(S, std::vector<CacheLine>(E));
    for (int i = 0; i < S; ++i) {
        for (auto &line : cache_sets[i]) {
            line.data.resize(B);
        }
    }
}

CoreStats L1Cache::getStats() const {
    return stats;
}

int L1Cache::getSetIndex(uint32_t address) const {
    return (address >> b) & ((1 << s) - 1);
}

uint32_t L1Cache::getTag(uint32_t address) const {
    return address >> (s + b);
}

int L1Cache::findLineByTag(int set_index, uint32_t tag) const {
    for (int i = 0; i < E; ++i) {
        if (cache_sets[set_index][i].valid && cache_sets[set_index][i].tag == tag) {
            return i;
        }
    }
    return -1;
}

int L1Cache::findLRULine(int set_index) const {
    int min_counter = cache_sets[set_index][0].lru_counter;
    int min_index = 0;
    for (int i = 1; i < E; ++i) {
        int ctr = cache_sets[set_index][i].lru_counter;
        if (ctr < min_counter || cache_sets[set_index][i].state == MESIState::INVALID) {
            min_counter = ctr;
            min_index = i;
            if (cache_sets[set_index][i].state == MESIState::INVALID) break;
        }
    }
    return min_index;
}

void L1Cache::updateLRUCounter(int set_index, int line_index) {
    for (int i = 0; i < E; ++i) {
        if (cache_sets[set_index][i].valid) {
            cache_sets[set_index][i].lru_counter--;
        }
    }
    cache_sets[set_index][line_index].lru_counter = E - 1;
}