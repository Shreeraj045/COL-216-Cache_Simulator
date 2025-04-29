#include "../include/L1Cache.h"
#include "../include/CacheSimulator.h"
#include <iostream>
#include <algorithm>

// Reference the global DEBUG_MODE variable defined in main.cpp
extern bool DEBUG_MODE;

L1Cache::L1Cache(int id, int s_bits, int b_bits, int assoc)
    : core_id(id), s(s_bits), b(b_bits), E(assoc)
{
    S = 1 << s;
    B = 1 << b;
    cache_sets.resize(S, std::vector<CacheLine>(E));
    for (int i = 0; i < S; ++i)
    {
        for (auto &line : cache_sets[i])
        {
            line.data.resize(B);
        }
    }
}

CoreStats L1Cache::getStats() const
{
    return stats;
}

bool L1Cache::isBlocked() const
{
    return is_blocked;
}

void L1Cache::unblock(int cycle)
{
    is_blocked = false;
}

int L1Cache::getSetIndex(uint32_t address) const
{
    return (address >> b) & ((1 << s) - 1);
}

uint32_t L1Cache::getTag(uint32_t address) const
{
    return address >> (s + b);
}

int L1Cache::findLineByTag(int set_index, uint32_t tag) const
{
    for (int i = 0; i < E; ++i)
    {
        if (cache_sets[set_index][i].valid && cache_sets[set_index][i].tag == tag)
            return i;
    }
    return -1;
}

int L1Cache::findLRULine(int set_index) const
{
    int min_counter = cache_sets[set_index][0].lru_counter;
    int min_index = 0;
    for (int i = 1; i < E; ++i)
    {
        int ctr = cache_sets[set_index][i].lru_counter;
        if (!cache_sets[set_index][i].valid || ctr < min_counter)
        {
            min_counter = ctr;
            min_index = i;
            if (!cache_sets[set_index][i].valid)
                break;
        }
    }
    return min_index;
}

void L1Cache::updateLRUCounter(int set_index, int line_index)
{
    int old = cache_sets[set_index][line_index].lru_counter;
    for (int i = 0; i < E; ++i)
    {
        if (cache_sets[set_index][i].valid && cache_sets[set_index][i].lru_counter > old)
        {
            cache_sets[set_index][i].lru_counter--;
        }
    }
    cache_sets[set_index][line_index].lru_counter = E - 1;
}

void L1Cache::addExecutionCycle(int cycles)
{
    stats.execution_cycles += cycles;
}

void L1Cache::addIdleCycle(int cycles)
{
    stats.idle_cycles += cycles;
}

bool L1Cache::processMemoryRequest(const MemRef &mem_ref, int current_cycle,
                                   std::vector<BusRequest> &bus_reqs)
{
    if (is_blocked)
        return false;

    uint32_t address = mem_ref.address;
    bool is_write = mem_ref.is_write;

    if (DEBUG_MODE)
    {
        std::cout << "[CYCLE " << std::setw(6) << current_cycle << "] "
                  << "Core " << core_id << " processes "
                  << (is_write ? "WRITE" : "READ") << " at 0x"
                  << std::hex << address << std::dec << std::endl;
    }

    // Process request - count stats
    if (is_write)
        stats.write_count++;
    else
        stats.read_count++;

    int set_index = getSetIndex(address);
    uint32_t tag = getTag(address);
    int line_index = findLineByTag(set_index, tag);

    // HIT
    if (line_index != -1)
    {
        stats.cache_hits++;
        CacheLine &line = cache_sets[set_index][line_index];
        if (is_write)
        {
            switch (line.state)
            {
            case MESIState::MODIFIED:
                break;
            case MESIState::EXCLUSIVE:
                line.state = MESIState::MODIFIED;
                break;
            case MESIState::SHARED:
                // upgrade to modified
                bus_reqs.emplace_back(core_id, BusOperation::BUS_UPGR, address, current_cycle, 1);
                is_blocked = true;
                pending_request = mem_ref;
                return false;
            default:
                break;
            }
        }
        // read hit or write hit (M/E)
        updateLRUCounter(set_index, line_index);
        return true;
    }

    // MISS
    stats.cache_misses++;
    bool need_flush = false;
    BusRequest flush_req(0, BusOperation::FLUSH, 0, 0, 0);

    // check eviction
    bool eviction_needed = true;
    for (int i = 0; i < E; ++i)
    {
        if (!cache_sets[set_index][i].valid)
        {
            eviction_needed = false;
            break;
        }
    }
    if (eviction_needed)
    {
        int victim = findLRULine(set_index);
        CacheLine &victim_line = cache_sets[set_index][victim];
        stats.evictions++;
        if (victim_line.state == MESIState::MODIFIED)
        {
            stats.writebacks++;
            uint32_t va = (victim_line.tag << (s + b)) | (set_index << b);
            flush_req = BusRequest(core_id, BusOperation::FLUSH, va, current_cycle, 100);
            need_flush = true;
        }
        // evict it
        victim_line.valid = false;
        victim_line.state = MESIState::INVALID;
    }

    // main miss request
    BusOperation op = is_write ? BusOperation::BUS_RDX : BusOperation::BUS_RD;
    BusRequest miss_req(core_id, op, address, current_cycle, 100);
    if (need_flush)
        bus_reqs.push_back(flush_req);
    bus_reqs.push_back(miss_req);

    is_blocked = true;
    pending_request = mem_ref;
    return false;
}

void L1Cache::handleBusRequest(const BusRequest &bus_req, int current_cycle,
                               bool &provide_data, int &transfer_cycles)
{
    if (bus_req.core_id == core_id)
        return;
    provide_data = false;
    transfer_cycles = 0;
    uint32_t address = bus_req.address;
    int set_index = getSetIndex(address);
    uint32_t tag = getTag(address);
    int idx = findLineByTag(set_index, tag);
    if (idx == -1)
        return;
    CacheLine &line = cache_sets[set_index][idx];
    switch (bus_req.operation)
    {
    case BusOperation::BUS_RD:
        if (line.state != MESIState::INVALID)
        {
            line.state = MESIState::SHARED;
            provide_data = true;
            transfer_cycles = 2 * (B / 4);
        }
        break;
    case BusOperation::BUS_RDX:
        if (line.state != MESIState::INVALID)
        {
            line.state = MESIState::INVALID;
            line.valid = false;
            provide_data = true;
            transfer_cycles = 2 * (B / 4);
        }
        break;
    case BusOperation::BUS_UPGR:
        if (line.state == MESIState::SHARED || line.state == MESIState::EXCLUSIVE)
        {
            line.state = MESIState::INVALID;
            line.valid = false;
        }
        break;
    default:
        break;
    }
}

void L1Cache::completeMemoryRequest(int current_cycle, bool is_upgrade,
                                    bool received_data_from_cache,
                                    MESIState new_state)
{
    if (!is_blocked)
        return;

    // Debug instruction completion
    uint32_t address = pending_request.address;
    bool is_write = pending_request.is_write;

    if (DEBUG_MODE)
    {
        std::cout << "[CYCLE " << std::setw(6) << current_cycle << "] "
                  << "Core " << core_id << " completing "
                  << (is_write ? "WRITE" : "READ") << " at 0x"
                  << std::hex << address << std::dec
                  << " | New state: " << stateToString(new_state)
                  << " | Data from cache: " << (received_data_from_cache ? "YES" : "NO")
                  << std::endl;
    }

    int set_index = getSetIndex(address);
    uint32_t tag = getTag(address);
    int idx = findLineByTag(set_index, tag);
    if (is_upgrade)
    {
        if (idx != -1)
            cache_sets[set_index][idx].state = new_state;
    }
    else
    {
        if (idx == -1)
        {
            idx = findLRULine(set_index);
        }
        CacheLine &line = cache_sets[set_index][idx];
        line.valid = true;
        line.tag = tag;
        line.state = new_state;
        updateLRUCounter(set_index, idx);
    }
    is_blocked = false;
}

void L1Cache::printCacheState() const
{
    std::cout << "Core " << core_id << " Cache State:\n";
    for (int i = 0; i < S; ++i)
    {
        bool any = false;
        for (auto &l : cache_sets[i])
            if (l.valid)
                any = true;
        if (!any)
            continue;
        std::cout << "Set " << i << ": ";
        for (auto &l : cache_sets[i])
        {
            if (l.valid)
            {
                std::cout << std::hex << "0x" << l.tag << std::dec
                          << "(" << stateToString(l.state) << ") ";
            }
            else
            {
                std::cout << "-------- ";
            }
        }
        std::cout << "\n";
    }
}

std::string L1Cache::stateToString(MESIState state) const
{
    switch (state)
    {
    case MESIState::MODIFIED:
        return "M";
    case MESIState::EXCLUSIVE:
        return "E";
    case MESIState::SHARED:
        return "S";
    case MESIState::INVALID:
        return "I";
    default:
        return "?";
    }
}

int L1Cache::getBlockSize() const
{
    return B;
}