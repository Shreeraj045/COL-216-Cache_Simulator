#pragma once
#include <vector>
#include <fstream>
#include <queue>
#include <optional>
#include <cstdint>
#include <string>
#include "L1Cache.h"

// Statistics for bus activity
struct BusStats
{
    int invalidations = 0;
    long long data_traffic_bytes = 0;
    int transactions = 0; // Count of bus transactions
};

struct BusRequestComparator
{
    // Comparator for arbitration: lower core_id has higher priority
    bool operator()(const BusRequest &a, const BusRequest &b) const
    {
        return a.core_id > b.core_id;
    }
};

class CacheSimulator
{
public:
    CacheSimulator(int s, int E, int b);
    ~CacheSimulator(); // Adding a proper destructor
    bool loadTraces(const std::string &app_name);
    void runSimulation();
    void printResults(std::ofstream &outfile);

private:
    std::string trace_prefix; // Store trace file prefix for display
    int num_cores = 4;
    int s_bits;
    int E_assoc;
    int b_bits;
    std::vector<L1Cache> caches;
    std::vector<std::vector<MemRef>> trace_data;
    std::vector<size_t> trace_position;
    BusStats bus_stats;
    std::vector<BusStats> core_bus_stats; // Per-core bus statistics

    std::priority_queue<BusRequest, std::vector<BusRequest>, BusRequestComparator> bus_queue;
    bool bus_busy = false;
    int bus_free_cycle = 0;
    std::optional<BusRequest> current_bus;
    bool current_data_from_cache = false;
    MESIState current_new_state;
};