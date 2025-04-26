#pragma once
#include <vector>
#include <string>
#include <fstream>
#include <unordered_map>
#include <queue>
#include <cstdint>
#include "L1Cache.h"

// Bus operations for coherence protocol
enum class BusOperation { BUS_RD, BUS_RDX, BUS_UPGR, FLUSH, FLUSH_OPT };

// A request on the bus
struct BusRequest {
    int core_id;
    BusOperation operation;
    uint32_t address;
    int start_cycle;
    int duration;
    BusRequest(int id, BusOperation op, uint32_t addr, int cycle, int dur)
        : core_id(id), operation(op), address(addr), start_cycle(cycle), duration(dur) {}
};

// Statistics for bus activity
struct BusStats {
    int invalidations = 0;
    long long data_traffic_bytes = 0;
};

class CacheSimulator {
public:
    CacheSimulator(int s, int E, int b);
    bool loadTraces(const std::string &app_name);
    void runSimulation();
    void printResults(std::ofstream &outfile);

private:
    int num_cores = 4;
    std::vector<L1Cache> caches;
    std::vector<std::vector<MemRef>> trace_data;
    std::vector<size_t> trace_position;
    BusStats bus_stats;

    std::queue<BusRequest> bus_queue;
    bool bus_busy = false;
    int bus_free_cycle = 0;
    BusRequest *current_bus_transaction = nullptr;

    std::unordered_map<uint32_t, int> address_to_set;
};