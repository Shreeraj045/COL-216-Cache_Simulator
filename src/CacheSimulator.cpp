#include "../include/CacheSimulator.h"
#include <iostream>
#include <sstream>
#include <iomanip> // Add for formatted output

// Reference the global DEBUG_MODE variable defined in main.cpp
extern bool DEBUG_MODE;

// Helper function to get string representation of bus operation
std::string busOpToString(BusOperation op)
{
    switch (op)
    {
    case BusOperation::BUS_RD:
        return "BUS_RD";
    case BusOperation::BUS_RDX:
        return "BUS_RDX";
    case BusOperation::BUS_UPGR:
        return "BUS_UPGR";
    case BusOperation::FLUSH:
        return "FLUSH";
    default:
        return "UNKNOWN";
    }
}

// Helper function to print bus queue contents for debugging (priority_queue version)
void printBusQueue(std::priority_queue<BusRequest, std::vector<BusRequest>, BusRequestComparator> queue)
{
    if (queue.empty())
    {
        std::cout << "    Queue: [empty]" << std::endl;
        return;
    }

    std::cout << "    Queue: [";

    while (!queue.empty())
    {
        const BusRequest &br = queue.top();
        std::cout << "Core" << br.core_id << ":"
                  << busOpToString(br.operation)
                  << ":0x" << std::hex << br.address << std::dec;

        queue.pop();
        if (!queue.empty())
        {
            std::cout << ", ";
        }
    }
    std::cout << "]" << std::endl;
}

CacheSimulator::CacheSimulator(int s, int E, int b)
    : s_bits(s), E_assoc(E), b_bits(b)
{
    for (int i = 0; i < num_cores; ++i)
    {
        caches.emplace_back(i, s_bits, b_bits, E_assoc);
    }
    trace_position.resize(num_cores, 0);
    core_bus_stats.resize(num_cores); // Initialize per-core bus stats
}

bool CacheSimulator::loadTraces(const std::string &app_name)
{
    // For output display, extract just the base name
    size_t lastSlash = app_name.rfind('/');
    if (lastSlash != std::string::npos)
    {
        trace_prefix = app_name.substr(lastSlash + 1);
    }
    else
    {
        trace_prefix = app_name;
    }

    trace_data.resize(num_cores);

    for (int i = 0; i < num_cores; ++i)
    {
        // Construct trace filename - ensure proper path handling
        std::string filename = app_name + "_proc" + std::to_string(i) + ".trace";

        std::ifstream file(filename);
        if (!file.is_open())
        {
            std::cerr << "Error: Could not open trace file: '" << filename << "'" << std::endl;
            return false;
        }

        std::string line;
        while (std::getline(file, line))
        {
            if (line.empty())
                continue;
            std::istringstream iss(line);
            char op;
            std::string addr_str;
            if (!(iss >> op >> addr_str))
                continue;
            uint32_t address = (addr_str.rfind("0x", 0) == 0)
                                   ? std::stoul(addr_str, nullptr, 16)
                                   : std::stoul(addr_str, nullptr, 10);
            trace_data[i].push_back({op == 'W', address});
        }
    }
    return true;
}

void CacheSimulator::runSimulation()
{
    int remaining = num_cores;
    std::vector<bool> done(num_cores, false);
    int cycle = 0;

    std::cout << "===== SIMULATION START =====" << std::endl;

    while (remaining > 0)
    {
        if (DEBUG_MODE)
        {
            std::cout << "\n[CYCLE " << std::setw(6) << cycle << "] "
                      << "Remaining cores: " << remaining
                      << " | Queue size: " << bus_queue.size()
                      << " | Bus busy: " << (bus_busy ? "YES" : "NO") << std::endl;
            printBusQueue(bus_queue); // Print bus queue contents
        }

        // Start next bus transaction if bus is free
        if (!bus_busy && !bus_queue.empty())
        {
            // Arbitration: pick the request from the lowest core ID (priority_queue)
            BusRequest br = bus_queue.top();
            bus_queue.pop();

            if (DEBUG_MODE)
            {
                std::cout << "[CYCLE " << std::setw(6) << cycle << "] "
                          << "Arbitration: Selected core " << br.core_id << " bus request" << std::endl;
            }
            current_bus = br;

            // Count this transaction
            bus_stats.transactions++;
            core_bus_stats[br.core_id].transactions++;

            if (DEBUG_MODE)
            {
                std::cout << "[CYCLE " << std::setw(6) << cycle << "] "
                          << "BUS: Core " << br.core_id << " starts "
                          << busOpToString(br.operation)
                          << " for address 0x" << std::hex << br.address << std::dec << std::endl;
            }

            // Snooping - process properly based on the bus operation
            bool data_from_cache = false;
            int transfer_cycles = 0;

            // Updated based on clarification #21: Read miss allows cache-to-cache transfer, write miss always from memory
            if (br.operation == BusOperation::BUS_RD)
            {
                // For read miss, check other caches for the data (cache-to-cache transfer allowed)
                for (int i = 0; i < num_cores; ++i)
                {
                    if (i == br.core_id)
                        continue;

                    bool can_provide_data = false;
                    int this_transfer_cycles = 0;
                    caches[i].handleBusRequest(br, cycle, can_provide_data, this_transfer_cycles);

                    if (can_provide_data && !data_from_cache)
                    {
                        data_from_cache = true;
                        transfer_cycles = this_transfer_cycles;
                    }
                }

                // Read miss, set appropriate state
                current_new_state = data_from_cache ? MESIState::SHARED : MESIState::EXCLUSIVE;
            }
            else if (br.operation == BusOperation::BUS_RDX)
            {
                // For write miss, always fetch from memory per clarification #21
                // But still need to invalidate in other caches
                for (int i = 0; i < num_cores; ++i)
                {
                    if (i == br.core_id)
                        continue;

                    bool can_provide_data = false;
                    int this_transfer_cycles = 0;
                    caches[i].handleBusRequest(br, cycle, can_provide_data, this_transfer_cycles);

                    // Even if another cache has the data, we don't use it for BUS_RDX
                    // Data always comes from memory for write miss
                }

                // Always set to MODIFIED for write miss
                current_new_state = MESIState::MODIFIED;
                data_from_cache = false; // Force memory access for write miss
            }
            else if (br.operation == BusOperation::BUS_UPGR)
            {
                // Invalidate in other caches
                for (int i = 0; i < num_cores; ++i)
                {
                    if (i == br.core_id)
                        continue;

                    bool dummy = false;
                    int dummy2 = 0;
                    caches[i].handleBusRequest(br, cycle, dummy, dummy2);
                }
                current_new_state = MESIState::MODIFIED;
            }

            // Update data source flag
            current_data_from_cache = data_from_cache;

            // Calculate bus transaction duration
            int duration;
            if (br.operation == BusOperation::FLUSH || br.operation == BusOperation::FLUSH_OPT)
            {
                duration = 100; // Writeback to memory
            }
            else if (br.operation == BusOperation::BUS_RD)
            {
                // For read miss, use cache-to-cache if available
                if (data_from_cache)
                {
                    duration = transfer_cycles; // Cache-to-cache transfer (typically 2*N cycles)
                }
                else
                {
                    duration = 100; // Memory access
                }
            }
            else if (br.operation == BusOperation::BUS_RDX)
            {
                // For write miss, always use memory
                duration = 100; // Memory access
            }
            else // BUS_UPGR
            {
                duration = 1; // Just invalidation signals
            }

            // Update bus stats
            if (br.operation == BusOperation::BUS_UPGR || br.operation == BusOperation::BUS_RDX)
            {
                bus_stats.invalidations++;
                core_bus_stats[br.core_id].invalidations++;
            }

            // Update data traffic stats
            if (br.operation == BusOperation::BUS_RD && data_from_cache)
            {
                // For cache-to-cache transfers on read miss
                int blockSize = caches[br.core_id].getBlockSize();
                bus_stats.data_traffic_bytes += blockSize;
                core_bus_stats[br.core_id].data_traffic_bytes += blockSize;
            }
            else if (br.operation == BusOperation::BUS_RD || br.operation == BusOperation::BUS_RDX ||
                     br.operation == BusOperation::FLUSH || br.operation == BusOperation::FLUSH_OPT)
            {
                // For memory transfers, count full block size
                int blockSize = caches[br.core_id].getBlockSize();
                bus_stats.data_traffic_bytes += blockSize;
                core_bus_stats[br.core_id].data_traffic_bytes += blockSize;
            }

            bus_busy = true;
            bus_free_cycle = cycle + duration;
        }

        // Complete bus transaction
        if (bus_busy && cycle == bus_free_cycle)
        {
            if (current_bus.has_value())
            {
                BusRequest br = current_bus.value();
                auto &cache = caches[br.core_id];

                if (br.operation == BusOperation::BUS_UPGR)
                {
                    cache.completeMemoryRequest(cycle, true, false, current_new_state);
                }
                else if (br.operation == BusOperation::BUS_RD || br.operation == BusOperation::BUS_RDX)
                {
                    cache.completeMemoryRequest(cycle, false, current_data_from_cache, current_new_state);
                }
                else if (br.operation == BusOperation::FLUSH || br.operation == BusOperation::FLUSH_OPT)
                {
                    cache.unblock(cycle);
                }
            }

            bus_busy = false;
            current_bus = std::nullopt; // Use std::nullopt instead of reset() to avoid potential issues
        }

        // Process each core
        for (int i = 0; i < num_cores; ++i)
        {
            if (done[i])
                continue;

            if (!caches[i].isBlocked())
            {
                if (trace_position[i] < trace_data[i].size())
                {
                    const MemRef &ref = trace_data[i][trace_position[i]];
                    std::vector<BusRequest> brs;
                    bool completed = caches[i].processMemoryRequest(ref, cycle, brs);

                    // Enqueue bus requests from this cycle
                    if (!brs.empty())
                    {
                        for (auto &r : brs)
                        {
                            // Set the correct duration based on the operation
                            if (r.operation == BusOperation::BUS_UPGR)
                            {
                                r.duration = 1; // Invalidation signal is fast
                            }
                            else if (r.operation == BusOperation::FLUSH || r.operation == BusOperation::FLUSH_OPT)
                            {
                                r.duration = 100; // Writeback to memory
                            }
                            else
                            {
                                r.duration = 100; // Default for memory access
                            }

                            r.start_cycle = cycle;
                            bus_queue.push(r);
                        }

                        if (DEBUG_MODE)
                        {
                            std::cout << "[CYCLE " << std::setw(6) << cycle << "] "
                                      << "Core " << i << " added requests to queue:" << std::endl;
                            printBusQueue(bus_queue);
                        }
                    }

                    // Count cycle as execution only if the instruction completed this cycle
                    if (completed)
                    {
                        caches[i].recordInstruction(ref.is_write); // record read/write instruction
                        caches[i].addExecutionCycle(1);
                        trace_position[i]++;
                    }
                    else
                    {
                        caches[i].addIdleCycle(1);
                    }
                }
                else
                {
                    done[i] = true;
                    --remaining;
                }
            }
            else
            {
                caches[i].addIdleCycle(1); // Core is stalled waiting for cache miss or bus access
            }
        }
        ++cycle;
    }
}

void CacheSimulator::printResults(std::ofstream &outfile)
{
    // Print simulation parameters
    int block_size = 1 << b_bits;
    int num_sets = 1 << s_bits;
    int cache_size_kb = (num_sets * E_assoc * block_size) / 1024;

    std::cout << "Simulation Parameters:" << std::endl;
    std::cout << "Trace Prefix: " << trace_prefix << std::endl;
    std::cout << "Set Index Bits: " << s_bits << std::endl;
    std::cout << "Associativity: " << E_assoc << std::endl;
    std::cout << "Block Bits: " << b_bits << std::endl;
    std::cout << "Block Size (Bytes): " << block_size << std::endl;
    std::cout << "Number of Sets: " << num_sets << std::endl;
    std::cout << "Cache Size (KB per core): " << cache_size_kb << std::endl;
    std::cout << "MESI Protocol: Enabled" << std::endl;
    std::cout << "Write Policy: Write-back, Write-allocate" << std::endl;
    std::cout << "Replacement Policy: LRU" << std::endl;
    std::cout << "Bus: Central snooping bus" << std::endl
              << std::endl;

    // Print per-core statistics
    for (int i = 0; i < num_cores; ++i)
    {
        auto stats = caches[i].getStats();
        auto bus_stats_core = core_bus_stats[i];

        int total_instructions = stats.instruction_count;
        double miss_rate = (total_instructions > 0) ? (100.0 * stats.cache_misses / total_instructions) : 0.0;
        int total_cycles = stats.execution_cycles + stats.idle_cycles; // Include idle cycles in total

        std::cout << "Core " << i << " Statistics:" << std::endl;
        std::cout << "Total Instructions: " << total_instructions << std::endl;
        std::cout << "Total Reads: " << stats.read_count << std::endl;
        std::cout << "Total Writes: " << stats.write_count << std::endl;
        std::cout << "Total Execution Cycles: " << total_cycles << std::endl;
        // std::cout << "Active Cycles: " << stats.execution_cycles << std::endl;
        std::cout << "Idle Cycles: " << stats.idle_cycles << std::endl;
        std::cout << "Cache Misses: " << stats.cache_misses << std::endl;
        std::cout << "Cache Miss Rate: " << std::fixed << std::setprecision(2) << miss_rate << "%" << std::endl;
        std::cout << "Cache Evictions: " << stats.evictions << std::endl;
        std::cout << "Writebacks: " << stats.writebacks << std::endl;
        std::cout << "Bus Invalidations: " << bus_stats_core.invalidations << std::endl;
        std::cout << "Data Traffic (Bytes): " << bus_stats_core.data_traffic_bytes << std::endl
                  << std::endl;

        // Write to outfile if provided
        if (outfile.is_open())
        {
            outfile << "Core," << i << "\n"
                    << "Total Instructions," << total_instructions << "\n"
                    << "Total Reads," << stats.read_count << "\n"
                    << "Total Writes," << stats.write_count << "\n"
                    << "Total Execution Cycles," << total_cycles << "\n"
                    << "Active Cycles," << stats.execution_cycles << "\n"
                    << "Idle Cycles," << stats.idle_cycles << "\n"
                    << "Cache Misses," << stats.cache_misses << "\n"
                    << "Cache Miss Rate," << miss_rate << "\n"
                    << "Cache Evictions," << stats.evictions << "\n"
                    << "Writebacks," << stats.writebacks << "\n"
                    << "Bus Invalidations," << bus_stats_core.invalidations << "\n"
                    << "Data Traffic (Bytes)," << bus_stats_core.data_traffic_bytes << "\n\n";
        }
    }

// Print overall bus summary
// Print overall bus summary
    std::cout << "Overall Bus Summary:" << std::endl;
    std::cout << "Total Bus Transactions: " << bus_stats.transactions << std::endl;
    std::cout << "Total Bus Traffic (Bytes): " << bus_stats.data_traffic_bytes << std::endl;

    if (outfile.is_open())
    {
        outfile << "Bus Summary\n"
                << "Total Bus Transactions," << bus_stats.transactions << "\n"
                << "Total Bus Traffic (Bytes)," << bus_stats.data_traffic_bytes << "\n";
    }
}

CacheSimulator::~CacheSimulator()

{
    // Clear the current_bus optional to prevent issues during destruction
    current_bus = std::nullopt;

    // Clear any remaining bus requests
    while (!bus_queue.empty())
    {
        bus_queue.pop();
    }
}
