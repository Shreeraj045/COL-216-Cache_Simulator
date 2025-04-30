#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <list>

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

// Bus operations for coherence protocol
enum class BusOperation
{
    BUS_RD,
    BUS_RDX,
    BUS_UPGR,
    FLUSH,
    FLUSH_OPT
};

// A request on the bus
struct BusRequest
{
    int core_id;
    BusOperation operation;
    uint32_t address;
    int start_cycle;
    int duration;
    BusRequest(int id, BusOperation op, uint32_t addr, int cycle, int dur)
        : core_id(id), operation(op), address(addr), start_cycle(cycle), duration(dur) {}
};

// Core statistics
struct CoreStats
{
    int read_count = 0;
    int write_count = 0;
    int instruction_count = 0; // count of completed instructions
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
};

class L1Cache
{
public:
    L1Cache(int core_id, int s_bits, int b_bits, int assoc);
    CoreStats getStats() const;
    bool isBlocked() const;
    void unblock(int cycle);
    void recordInstruction(bool is_write); // record completed instruction

    // Process a memory request. Returns true if hit, false if miss or upgrade.
    // bus_reqs is filled with 0, 1, or 2 BusRequests to enqueue for eviction and/or miss.
    bool processMemoryRequest(const MemRef &mem_ref, int current_cycle,
                              std::vector<BusRequest> &bus_reqs);

    // Handle a bus request from another core for snooping
    void handleBusRequest(const BusRequest &bus_req, int current_cycle,
                          bool &provide_data, int &transfer_cycles);

    // Complete a memory request after a bus transaction finishes
    void completeMemoryRequest(int current_cycle, bool is_upgrade,
                               bool received_data_from_cache,
                               MESIState new_state);

    void printCacheState() const;
    std::string stateToString(MESIState state) const;
    int getBlockSize() const;
    // Methods for simulation loop to account cycles
    void addExecutionCycle(int cycles);
    void addIdleCycle(int cycles);

private:
    int core_id;
    int S; // number of sets
    int E; // associativity
    int B; // block size (bytes)
    int s; // set index bits
    int b; // block offset bits

    std::vector<std::list<CacheLine>> cache_sets;
    CoreStats stats;
    bool is_blocked = false;
    MemRef pending_request;

    int getSetIndex(uint32_t address) const;
    uint32_t getTag(uint32_t address) const;
    CacheLine *findLineByTag(int set_index, uint32_t tag);
    void moveToFront(int set_index, std::list<CacheLine>::iterator it);
};