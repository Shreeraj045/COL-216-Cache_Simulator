#include "L1Cache.h"
#include <iostream>
#include <iomanip>
using namespace std;

L1Cache::L1Cache(int id, int s_bits, int b_bits, int assoc){   
    core_id = id;
    s = s_bits;
    b = b_bits;
    // S = no. of sets, B = block size
    // E = n-way set associativity
    S = 1 << s_bits;
    B = 1 << b_bits;
    E = assoc;

    cache_sets.resize(S, vector<CacheLine>(E));
    for (int i = 0; i < S; ++i) {
        for (auto &line : cache_sets[i]) {
            line.data.resize(B);
        }
    }
}

CoreStats L1Cache::getStats() const {
    return stats;
}

// adress -> tagbits | set s bits | block b bits

int L1Cache::getSetIndex(uint32_t address) const {
    uint32_t temp = address >> b;
    uint32_t s_ones = (1 << s) -1 ;
    return temp & s_ones;
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

// least recently used has the smallest counter
int L1Cache::findLRULine(int set_index) const {
    int min_counter = cache_sets[set_index][0].lru_counter;
    int min_index = 0;
    for (int i = 1; i < E; ++i) {
        int ctr = cache_sets[set_index][i].lru_counter;
        if (ctr < min_counter || cache_sets[set_index][i].state == MESIState::INVALID) {
            min_counter = ctr;
            min_index = i;
            //if invalid found pick it 
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

bool L1Cache::isBlocked() const {
    return is_blocked;
}

void L1Cache::unblock(int cycle) {
    is_blocked = false;
    unblock_cycle = cycle;
}

string L1Cache::stateToString(MESIState state) const {
    switch (state) {
        case MESIState::MODIFIED: return "M";
        case MESIState::EXCLUSIVE: return "E";
        case MESIState::SHARED: return "S";
        case MESIState::INVALID: return "I";
        default: return "?";
    }
}

bool L1Cache::processMemoryRequest(const MemRef& mem_ref, int current_cycle, BusRequest& bus_req, bool& needs_bus) {
    needs_bus = false;

    uint32_t address = mem_ref.address;
    bool is_write = mem_ref.is_write;

    // Update read/write counts
    if (is_write) {
        stats.write_count++;
    } else {
        stats.read_count++;
    }

    int set_index = getSetIndex(address);
    uint32_t tag = getTag(address);
    int line_index = findLineByTag(set_index, tag);

    // Cache hit
    if (line_index != -1) {
        stats.cache_hits++;
        CacheLine& line = cache_sets[set_index][line_index];

        if (is_write) {
            // Write hit - state transitions depend on current state
            switch (line.state) {
                case MESIState::MODIFIED: break;
                case MESIState::EXCLUSIVE:
                // Exclusive to Modified - silent transition, no bus request
                    line.state = MESIState::MODIFIED;
                    break;
                case MESIState::SHARED:
                    // Shared to Modified - need bus upgrade
                    needs_bus = true;
                    // one cycle needed , upgrade ownership
                    bus_req = BusRequest(core_id, BusOperation::BUS_UPGR, address, current_cycle, 1);
                    is_blocked = true;
                    pending_request = mem_ref;
                    return false;
                case MESIState::INVALID:
                    // Should not happen for a hit
                    cerr << "Error: Invalid state hit in cache" << endl;
                    break;
            }
        }
        // Read hit - no state change needed

        // Update LRU status
        updateLRUCounter(set_index, line_index);
        return true;
    }

    // Cache miss
    stats.cache_misses++;

    // Determine eviction if necessary
    bool needs_eviction = true;
    for (int i = 0; i < E; ++i) {
        if (cache_sets[set_index][i].state == MESIState::INVALID) {
            needs_eviction = false;
            break;
        }
    }

    if (needs_eviction) {
        int victim_line = findLRULine(set_index);
        CacheLine& victim = cache_sets[set_index][victim_line];

        stats.evictions++;

        if (victim.state == MESIState::MODIFIED) {
            // Need to write back dirty line, 100 cycles needed
            stats.writebacks++;
            uint32_t addr = (victim.tag << (s+b)) | (set_index << b);
            bus_req = BusRequest(core_id, BusOperation::FLUSH, addr, current_cycle, 100);
            needs_bus = true;
        }
    }

    // Issue bus request for the miss
    needs_bus = true;
    if (is_write) {
        // Write miss - BusRdX (read with intent to modify)
        bus_req = BusRequest(core_id, BusOperation::BUS_RDX, address, current_cycle, 100);
    } else {
        // Read miss - BusRd
        bus_req = BusRequest(core_id, BusOperation::BUS_RD, address, current_cycle, 100);
    }

    is_blocked = true;
    pending_request = mem_ref;
    return false;
}