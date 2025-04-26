#include "CacheSimulator.h"
#include <iostream>
#include <fstream>
#include <sstream>

using namespace std;

CacheSimulator::CacheSimulator(int s, int E, int b) {
    // Initialize caches for each core
    for (int i = 0; i < num_cores; i++) {
        caches.emplace_back(i, s, E, b);
    }
    // Initialize trace positions
    trace_position.resize(num_cores, 0);
}

bool CacheSimulator::loadTraces(const string &app_name) {
    trace_data.resize(num_cores);
    for (int i = 0; i < num_cores; i++) {
        string filename = app_name + "_proc" + to_string(i) + ".trace";
        ifstream file(filename);
        if (!file.is_open()) {
            cerr << "Error: Could not open file " << filename << endl;
            return false;
        }
        string line;
        while (getline(file, line)) {
            if (line.empty()) continue;
            istringstream iss(line);
            char op;
            string addr_str;
            if (!(iss >> op >> addr_str)) {
                cerr << "Error parsing line: " << line << endl;
                continue;
            }
            uint32_t address;
            if (addr_str.rfind("0x", 0) == 0) {
                address = stoul(addr_str, nullptr, 16);
            } else {
                address = stoul(addr_str, nullptr, 10);
            }
            MemRef mem_ref{op == 'W', address};
            trace_data[i].push_back(mem_ref);
        }
        file.close();
    }
    return true;
}

void CacheSimulator::runSimulation() {
    cout << "TODO" << endl;
}

void CacheSimulator::printResults(ofstream &outfile) {
    for (int i = 0; i < num_cores; i++) {
        CoreStats stats = caches[i].getStats();
        cout << "Core " << i << " Statistics:" << endl;
        cout << "  Read instructions: " << stats.read_count << endl;
        cout << "  Write instructions: " << stats.write_count << endl;
        cout << "  Total execution cycles: " << stats.execution_cycles << endl;
        cout << "  Idle cycles: " << stats.idle_cycles << endl;
        cout << "  Cache miss rate: " << (double)stats.cache_misses / (stats.cache_hits + stats.cache_misses) << endl;
        cout << "  Cache evictions: " << stats.evictions << endl;
        cout << "  Cache writebacks: " << stats.writebacks << endl;
        if (outfile.is_open()) {
            outfile << "Core," << i << endl;
            outfile << "Read instructions," << stats.read_count << endl;
            outfile << "Write instructions," << stats.write_count << endl;
            outfile << "Total execution cycles," << stats.execution_cycles << endl;
            outfile << "Idle cycles," << stats.idle_cycles << endl;
            outfile << "Cache miss rate," << (double)stats.cache_misses / (stats.cache_hits + stats.cache_misses) << endl;
            outfile << "Cache evictions," << stats.evictions << endl;
            outfile << "Cache writebacks," << stats.writebacks << endl;
            outfile << endl;
        }
    }
    cout << "\nBus Statistics:" << endl;
    cout << "  Invalidations: " << bus_stats.invalidations << endl;
    cout << "  Data traffic: " << bus_stats.data_traffic_bytes << " bytes" << endl;
    if (outfile.is_open()) {
        outfile << "Bus Statistics" << endl;
        outfile << "Invalidations," << bus_stats.invalidations << endl;
        outfile << "Data traffic (bytes)," << bus_stats.data_traffic_bytes << endl;
    }
}