#pragma once
#include <cstdint>
#include <vector>
#include <string>

// MESI cache coherence states
enum class MESIState
{
    MODIFIED,
    EXCLUSIVE,
    SHARED,
    INVALID
};

// Reference to a memory operation
struct MemRef
{
    bool is_write;
    uint32_t address;
};

enum class BusOperation;
struct BusRequest;

// Core statistics
struct CoreStats
{
    int read_count = 0;
    int write_count = 0;
    int execution_cycles = 0;
    int idle_cycles = 0;
    int cache_misses = 0;
    int cache_hits = 0;
    int evictions = 0;
    int writebacks = 0;
};

// A single cache line
struct CacheLine
{
    bool valid = false;
    uint32_t tag = 0;
    MESIState state = MESIState::INVALID;
    std::vector<uint8_t> data;
    int lru_counter = 0;
};

// L1Cache interface
class L1Cache
{
public:
    L1Cache(int core_id, int s, int b, int E);
    CoreStats getStats() const;
    bool isBlocked() const;
    void unblock(int cycle);

    // Process a memory request, return true if hit, false if miss
    // If miss, sets needs_bus to true and populates bus_req
    bool processMemoryRequest(const MemRef &mem_ref, int current_cycle,
                              BusRequest &bus_req, bool &needs_bus);

    // Handle a bus request from another core
    // Set provide_data to true if this cache can provide data
    void handleBusRequest(const BusRequest &bus_req, int current_cycle,
                          bool &provide_data, int &transfer_cycles);

    // Complete a memory request after bus transaction
    void completeMemoryRequest(int current_cycle, bool is_hit,
                               bool received_data_from_cache,
                               MESIState new_state);

    // Debug functions
    void printCacheState() const;
    std::string stateToString(MESIState state) const;

private:
    int core_id;
    int S; // number of sets
    int E; // associativity
    int B; // block size (bytes)
    int s; // set index bits
    int b; // block offset bits

    std::vector<std::vector<CacheLine>> cache_sets;
    CoreStats stats;
    bool is_blocked = false;
    int unblock_cycle = -1;
    MemRef pending_request;

    int getSetIndex(uint32_t address) const;
    uint32_t getTag(uint32_t address) const;
    int findLineByTag(int set_index, uint32_t tag) const;
    int findLRULine(int set_index) const;
    void updateLRUCounter(int set_index, int line_index);
};