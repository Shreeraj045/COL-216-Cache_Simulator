#include "../include/L1Cache.h"
#include "../include/CacheSimulator.h"
#include <iostream>
#include <algorithm>
#include <iomanip>

// Reference the global DEBUG_MODE variable defined in main.cpp
extern bool DEBUG_MODE;

L1Cache::L1Cache(int id, int s_bits, int b_bits, int assoc)
    : core_id(id), E(assoc), s(s_bits), b(b_bits)
{
    S = 1 << s;
    B = 1 << b;
    cache_sets.resize(S);
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

CacheLine *L1Cache::findLineByTag(int set_index, uint32_t tag)
{
    auto &set = cache_sets[set_index];
    for (auto it = set.begin(); it != set.end(); ++it)
    {
        if (it->valid && it->tag == tag && it->state != MESIState::INVALID)
        {
            moveToFront(set_index, it);
            return &(*it);
        }
    }
    return nullptr;
}

void L1Cache::moveToFront(int set_index, std::list<CacheLine>::iterator it)
{
    if (it != cache_sets[set_index].begin())
    {
        cache_sets[set_index].splice(cache_sets[set_index].begin(), cache_sets[set_index], it);
    }
}

void L1Cache::addExecutionCycle(int cycles)
{
    stats.execution_cycles += cycles;
}

void L1Cache::addIdleCycle(int cycles)
{
    stats.idle_cycles += cycles;
}

int L1Cache::getBlockSize() const
{
    return B;
}

std::string L1Cache::stateToString(MESIState state) const
{
    switch (state)
    {
    case MESIState::MODIFIED:
        return "MODIFIED";
    case MESIState::EXCLUSIVE:
        return "EXCLUSIVE";
    case MESIState::SHARED:
        return "SHARED";
    case MESIState::INVALID:
        return "INVALID";
    default:
        return "UNKNOWN";
    }
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

    int set_index = getSetIndex(address);
    uint32_t tag = getTag(address);

    // Check for hit
    CacheLine *line = findLineByTag(set_index, tag);

    // HIT
    if (line != nullptr)
    {
        stats.cache_hits++;

        // Line is already moved to front by findLineByTag
        if (!is_write)
        {
            // Read hit - no state change, takes 1 cycle
            return true;
        }

        // Write hit - handle based on current state
        switch (line->state)
        {
        case MESIState::MODIFIED:
            // Already modified, no state change needed
            return true;

        case MESIState::EXCLUSIVE:
            // Silently transition to Modified
            line->state = MESIState::MODIFIED;
            return true;

        case MESIState::SHARED:
            // Need to invalidate copies in other caches
            bus_reqs.emplace_back(core_id, BusOperation::BUS_UPGR, address, current_cycle, 1);
            is_blocked = true;
            pending_request = mem_ref;
            return false;

        case MESIState::INVALID:
            // Should not happen since we check for INVALID in findLineByTag, but just in case
            return false;
        }
    }

    // MISS
    stats.cache_misses++;

    auto &set = cache_sets[set_index];
    bool eviction_needed = (int)set.size() >= E;

    if (eviction_needed)
    {
        stats.evictions++;

        // Get the LRU line (back of list)
        CacheLine &victim_line = set.back();

        if (victim_line.state == MESIState::MODIFIED)
        {
            stats.writebacks++;

            // Calculate the physical address of the victim line
            uint32_t victim_addr = (victim_line.tag << (s + b)) | (set_index << b);

            // Create a flush request for the writeback
            bus_reqs.emplace_back(core_id, BusOperation::FLUSH, victim_addr, current_cycle, 100);
        }

        // Remove the LRU line
        set.pop_back();
    }

    // Issue the appropriate bus request for the miss
    BusOperation op = is_write ? BusOperation::BUS_RDX : BusOperation::BUS_RD;
    bus_reqs.emplace_back(core_id, op, address, current_cycle, 0);

    // Mark cache as blocked while waiting for the request
    is_blocked = true;
    pending_request = mem_ref;
    return false;
}

void L1Cache::handleBusRequest(const BusRequest &bus_req, int current_cycle,
                               bool &provide_data, int &transfer_cycles)
{
    // Don't process our own requests
    if (bus_req.core_id == core_id)
        return;

    provide_data = false;
    transfer_cycles = 0;

    uint32_t address = bus_req.address;
    int set_index = getSetIndex(address);
    uint32_t tag = getTag(address);

    CacheLine *line = findLineByTag(set_index, tag);
    if (line == nullptr)
        return;

    switch (bus_req.operation)
    {
    case BusOperation::BUS_RD:
        if (line->state != MESIState::INVALID)
        {
            line->state = MESIState::SHARED;
            provide_data = true;
            transfer_cycles = 2 * (getBlockSize() / 4);
        }
        break;

    case BusOperation::BUS_RDX:
        if (line->state != MESIState::INVALID)
        {
            // Another core wants exclusive access, invalidate our copy
            if (line->state == MESIState::MODIFIED)
            {
                // We have the most recent data, provide it
                stats.writebacks++;
                line->state = MESIState::INVALID;
                line->valid = false;
                provide_data = true;
                transfer_cycles = 200;
            }
            else
            {
                line->state = MESIState::INVALID;
                line->valid = false;
                provide_data = true;
                transfer_cycles = 2 * (getBlockSize() / 4);
            }
        }
        break;

    case BusOperation::BUS_UPGR:
        if (line->state == MESIState::SHARED)
        {
            line->state = MESIState::INVALID;
            line->valid = false;
        }
        break;

    case BusOperation::FLUSH:
    case BusOperation::FLUSH_OPT:
        // Nothing to do, memory controller handles this
        break;
    }
}

void L1Cache::completeMemoryRequest(int current_cycle, bool is_upgrade,
                                    bool received_data_from_cache,
                                    MESIState new_state)
{
    if (!is_blocked)
        return;

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

    if (is_upgrade)
    {
        // Upgrade existing line state
        CacheLine *line = findLineByTag(set_index, tag);
        if (line != nullptr)
        {
            line->state = new_state;
            line->valid = true;
            line->tag = tag;
        }
    }
    else
    {
        auto &set = cache_sets[set_index];
        set.push_front(CacheLine());

        // Set up the new line
        CacheLine &line = set.front();
        line.valid = true;
        line.tag = tag;
        line.state = new_state;
        line.data.resize(B);
    }

    // Unblock the cache as the memory request is complete
    is_blocked = false;
}

void L1Cache::recordInstruction(bool is_write)
{
    stats.instruction_count++;
    if (is_write)
    {
        stats.write_count++;
    }
    else
    {
        stats.read_count++;
    }
}