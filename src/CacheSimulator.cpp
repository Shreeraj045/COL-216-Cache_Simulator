#include "../include/CacheSimulator.h"
#include <iostream>
#include <sstream>
#include <iomanip> // Add for formatted output

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

    while (remaining > 0)
    {
        // Start next bus transaction if bus is free
        if (!bus_busy && !bus_queue.empty())
        {
            BusRequest br = bus_queue.front();
            bus_queue.pop();
            current_bus = br;

            // Count this transaction
            bus_stats.transactions++;
            core_bus_stats[br.core_id].transactions++;

            // Snooping
            bool data_cache = false;
            int transfer_cycles = 0;
            if (br.operation == BusOperation::BUS_RD)
            {
                for (int i = 0; i < num_cores; ++i)
                {
                    if (i == br.core_id)
                        continue;
                    bool provide = false;
                    int tcycles = 0;
                    caches[i].handleBusRequest(br, cycle, provide, tcycles);
                    if (provide)
                    {
                        data_cache = true;
                        transfer_cycles = tcycles;
                    }
                }
                current_data_from_cache = data_cache;
                current_new_state = data_cache ? MESIState::SHARED : MESIState::EXCLUSIVE;
            }
            else
            {
                // Invalidate or write intent
                for (int i = 0; i < num_cores; ++i)
                {
                    if (i == br.core_id)
                        continue;
                    bool dummy = false;
                    int dummy2 = 0;
                    caches[i].handleBusRequest(br, cycle, dummy, dummy2);
                }
                current_data_from_cache = false;
                current_new_state = MESIState::MODIFIED;
            }
            // Update bus stats
            if (br.operation == BusOperation::BUS_UPGR || br.operation == BusOperation::BUS_RDX)
            {
                bus_stats.invalidations++;
                core_bus_stats[br.core_id].invalidations++;
            }
            if (br.operation == BusOperation::BUS_RD || br.operation == BusOperation::BUS_RDX)
            {
                int Bsize = caches[br.core_id].getBlockSize();
                bus_stats.data_traffic_bytes += Bsize;
                core_bus_stats[br.core_id].data_traffic_bytes += Bsize;
            }
            else if (br.operation == BusOperation::FLUSH)
            {
                int Bsize = caches[br.core_id].getBlockSize();
                bus_stats.data_traffic_bytes += Bsize;
                core_bus_stats[br.core_id].data_traffic_bytes += Bsize;
            }
            // Determine duration
            int dur = br.duration;
            if (br.operation == BusOperation::BUS_RD && data_cache)
                dur = transfer_cycles;
            bus_busy = true;
            bus_free_cycle = cycle + dur;
        }

        // Complete bus transaction
        if (bus_busy && cycle == bus_free_cycle)
        {
            BusRequest br = *current_bus;
            auto &cache = caches[br.core_id];
            if (br.operation == BusOperation::BUS_UPGR)
            {
                cache.completeMemoryRequest(cycle, true, false, current_new_state);
            }
            else if (br.operation == BusOperation::BUS_RD || br.operation == BusOperation::BUS_RDX)
            {
                cache.completeMemoryRequest(cycle, false, current_data_from_cache, current_new_state);
            }
            else if (br.operation == BusOperation::FLUSH)
            {
                cache.unblock(cycle);
            }
            bus_busy = false;
            current_bus.reset();
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
                    bool hit = caches[i].processMemoryRequest(ref, cycle, brs);
                    caches[i].addExecutionCycle(1);
                    if (!brs.empty())
                    {
                        for (auto &r : brs)
                        {
                            BusRequest q = r;
                            q.start_cycle = cycle;
                            bus_queue.push(q);
                        }
                    }
                    if (hit)
                        trace_position[i]++;
                }
                else
                {
                    done[i] = true;
                    --remaining;
                }
            }
            else
            {
                caches[i].addIdleCycle(1);
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

        int total_instructions = stats.read_count + stats.write_count;
        double miss_rate = 100.0 * stats.cache_misses / total_instructions;
        int total_cycles = stats.execution_cycles + stats.idle_cycles; // Include idle cycles in total

        std::cout << "Core " << i << " Statistics:" << std::endl;
        std::cout << "Total Instructions: " << total_instructions << std::endl;
        std::cout << "Total Reads: " << stats.read_count << std::endl;
        std::cout << "Total Writes: " << stats.write_count << std::endl;
        std::cout << "Total Execution Cycles: " << total_cycles << std::endl;
        std::cout << "Active Cycles: " << stats.execution_cycles << std::endl;
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