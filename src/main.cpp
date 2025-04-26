#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>
#include <sstream>
#include <bitset>
#include <map>
#include <unordered_map>
#include <queue>
#include <algorithm>
using namespace std;

enum class MESIState
{
    MODIFIED,
    EXCLUSIVE,
    SHARED,
    INVALID
};

enum class BusOperation
{
    BUS_RD,   // Read request
    BUS_RDX,  // Read with intent to modify
    BUS_UPGR, // Upgrade request (S->M)
    FLUSH,    // Write back to memory
    FLUSH_OPT // Optimized flush (directly to another cache)
};

struct CacheLine
{
    bool valid = false;
    uint32_t tag = 0;
    MESIState state = MESIState::INVALID;
    vector<uint8_t> data; // Cache block data
    int lru_counter = 0;  // For LRU replacement
};

struct BusRequest
{
    int core_id;
    BusOperation operation;
    uint32_t address;
    int start_cycle;
    int duration; // How many cycles this request takes

    BusRequest(int id, BusOperation op, uint32_t addr, int cycle, int dur)
        : core_id(id), operation(op), address(addr), start_cycle(cycle), duration(dur) {}
};

struct MemRef
{
    bool is_write; // true for write, false for read
    uint32_t address;
};

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

struct BusStats
{
    int invalidations = 0;
    long long data_traffic_bytes = 0;
};

class L1Cache
{
private:
    int core_id;
    int S; // number of sets
    int E; // associ.
    int B; // Block size
    int s; // 2^s=S -> set index bits
    int b; // 2^b = B -> block ofset

    vector<vector<CacheLine>> cache_sets;
    CoreStats stats;
    bool is_blocked = false;
    int unblock_cycle = -1;
    MemRef pending_request;

    // Extract set index from address
    int getSetIndex(uint32_t address)
    {
        return (address >> b) & ((1 << s) - 1);
    }

    // Extract tag from address
    uint32_t getTag(uint32_t address)
    {
        return address >> (s + b);
    }

    // Find a cache line in a set by tag
    int findLineByTag(int set_index, uint32_t tag)
    {
        for (int i = 0; i < E; i++)
        {
            if (cache_sets[set_index][i].valid && cache_sets[set_index][i].tag == tag)
            {
                return i;
            }
        }
        return -1; // Not found
    }

    // Find the LRU line in a set
    int findLRULine(int set_index)
    {
        int min_counter = cache_sets[set_index][0].lru_counter;
        int min_index = 0;

        for (int i = 1; i < E; i++)
        {
            if (cache_sets[set_index][i].lru_counter < min_counter)
            {
                min_counter = cache_sets[set_index][i].lru_counter;
                min_index = i;
            }
            // Prioritize invalid lines
            if (cache_sets[set_index][i].state == MESIState::INVALID)
            {
                return i;
            }
        }
        return min_index;
    }

    // Update LRU counters when a line is accessed
    void updateLRUCounter(int set_index, int line_index)
    {
        // Increment the accessed line's counter to be the most recently used
        for (int i = 0; i < E; i++)
        {
            if (cache_sets[set_index][i].valid)
            {
                cache_sets[set_index][i].lru_counter -= 1;
            }
        }
        cache_sets[set_index][line_index].lru_counter = E - 1;
    }

public:
    L1Cache(int id, int s, int b, int e)
        : core_id(id), S(1 << s), b(b), s(s), E(e)
    {
        // Calculate number of sets and block size
        S = 1 << s;
        B = 1 << b;

        // Initialize cache sets
        cache_sets.resize(S, vector<CacheLine>(E));
        for (int i = 0; i < S; i++)
        {
            for (int j = 0; j < E; j++)
            {
                cache_sets[i][j].data.resize(B);
            }
        }
    }
    CoreStats getStats() const
    {
        return stats;
    }
};

class CacheSimulator
{
private:
    int num_cores = 4;
    vector<L1Cache> caches;
    vector<vector<MemRef>> trace_data;
    vector<size_t> trace_position;
    BusStats bus_stats;

    queue<BusRequest> bus_queue;
    bool bus_busy = false;
    int bus_free_cycle = 0;
    BusRequest *current_bus_transaction = nullptr;

    // Address to set index mapping for coherence
    unordered_map<uint32_t, int> address_to_set;

public:
    CacheSimulator(int s, int E, int b)
    {
        // Initialize caches for each core
        for (int i = 0; i < num_cores; i++)
        {
            caches.emplace_back(i, s, E, b);
        }

        // Initialize trace positions
        trace_position.resize(num_cores, 0);
    }
    // Load trace files
    bool loadTraces(const string &app_name)
    {
        trace_data.resize(num_cores);

        for (int i = 0; i < num_cores; i++)
        {
            string filename = app_name + "_proc" + to_string(i) + ".trace";
            ifstream file(filename);

            if (!file.is_open())
            {
                cerr << "Error: Could not open file " << filename << endl;
                return false;
            }

            string line;
            while (getline(file, line))
            {
                if (line.empty())
                    continue;

                istringstream iss(line);
                char op;
                string addr_str;

                if (!(iss >> op >> addr_str))
                {
                    cerr << "Error parsing line: " << line << endl;
                    continue;
                }

                // Parse the address (supports both 0x prefix and without)
                uint32_t address;
                if (addr_str.substr(0, 2) == "0x")
                {
                    address = stoul(addr_str, nullptr, 16);
                }
                else
                {
                    address = stoul(addr_str, nullptr, 10);
                }

                MemRef mem_ref;
                mem_ref.is_write = (op == 'W');
                mem_ref.address = address;

                trace_data[i].push_back(mem_ref);
            }

            file.close();
        }

        return true;
    }
    void runSimulation()
    {
        cout << "TODO" << endl;
    }
    // Print simulation results
    void printResults(ofstream &outfile)
    {
        for (int i = 0; i < num_cores; i++)
        {
            CoreStats stats = caches[i].getStats();

            cout << "Core " << i << " Statistics:" << endl;
            cout << "  Read instructions: " << stats.read_count << endl;
            cout << "  Write instructions: " << stats.write_count << endl;
            cout << "  Total execution cycles: " << stats.execution_cycles << endl;
            cout << "  Idle cycles: " << stats.idle_cycles << endl;
            cout << "  Cache miss rate: " << (double)stats.cache_misses / (stats.cache_hits + stats.cache_misses) << endl;
            cout << "  Cache evictions: " << stats.evictions << endl;
            cout << "  Cache writebacks: " << stats.writebacks << endl;

            if (outfile.is_open())
            {
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

        if (outfile.is_open())
        {
            outfile << "Bus Statistics" << endl;
            outfile << "Invalidations," << bus_stats.invalidations << endl;
            outfile << "Data traffic (bytes)," << bus_stats.data_traffic_bytes << endl;
        }
    }
    // int getBlockSize() const
    // {
    //     return caches[0].getB();
    // }
};

void printUsage(const string &program_name)
{
    cout << "Usage: " << program_name << " [-t <appname>] [-s <s>] [-E <E>] [-b <b>] [-o <outfilename>]" << endl;
    cout << "Defaults: -t sample, -s 5 (32 sets), -E 4 (4-way), -b 5 (32B block)" << endl;
    cout << "Options:" << endl;
    cout << "  -t <appname>    base name of application traces (default 'sample')" << endl;
    cout << "  -s <s>          number of set index bits, sets = 2^s (default 5)" << endl;
    cout << "  -E <E>          associativity (default 4)" << endl;
    cout << "  -b <b>          number of block offset bits, block bytes = 2^b (default 5)" << endl;
    cout << "-o <outfilename>: logs output in file for plotting etc." << endl;
    cout << "-h: prints this help" << endl;
}
int main(int argc, char *argv[])
{
    // Default parameters
    string trace_name = "sample"; // default application name
    int s = 5;                    // Set index bits (default => 2^5=32 sets)
    int E = 4;                    // Associativity (default 4-way)
    int b = 5;                    // Block bits (default => 2^5=32B blocks)
    string outfile_name = "";

    // Parse command line arguments
    for (int i = 1; i < argc; i++)
    {
        string arg = argv[i];

        if (arg == "-h")
        {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg == "-t")
        {
            if (i + 1 < argc)
            {
                trace_name = argv[++i];
            }
            else
            {
                cerr << "Error: -t requires a trace file name." << endl;
                return 1;
            }
        }
        else if (arg == "-s")
        {
            if (i + 1 < argc)
            {
                s = stoi(argv[++i]);
            }
            else
            {
                cerr << "Error: -s requires a number." << endl;
                return 1;
            }
        }
        else if (arg == "-E")
        {
            if (i + 1 < argc)
            {
                E = stoi(argv[++i]);
            }
            else
            {
                cerr << "Error: -E requires a number." << endl;
                return 1;
            }
        }
        else if (arg == "-b")
        {
            if (i + 1 < argc)
            {
                b = stoi(argv[++i]);
            }
            else
            {
                cerr << "Error: -b requires a number." << endl;
                return 1;
            }
        }
        else if (arg == "-o")
        {
            if (i + 1 < argc)
            {
                outfile_name = argv[++i];
            }
            else
            {
                cerr << "Error: -o requires a file name." << endl;
                return 1;
            }
        }
        else
        {
            cerr << "Unknown option: " << arg << endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    // Validate parameters
    if (s <= 0 || E <= 0 || b <= 0)
    {
        cerr << "Error: Invalid cache parameters." << endl;
        printUsage(argv[0]);
        return 1;
    }

    // Create simulator
    CacheSimulator simulator(s, E, b);

    // Load traces
    if (!simulator.loadTraces(trace_name))
    {
        cerr << "Error loading trace files." << endl;
        return 1;
    }

    // Run simulation
    simulator.runSimulation();

    // Open output file if specified
    ofstream outfile;
    if (!outfile_name.empty())
    {
        outfile.open(outfile_name);
        if (!outfile.is_open())
        {
            cerr << "Warning: Could not open output file " << outfile_name << endl;
        }
    }

    // Print results
    simulator.printResults(outfile);

    // Close output file
    if (outfile.is_open())
    {
        outfile.close();
    }

    return 0;
}
