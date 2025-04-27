#include <cstdint>
#include <iostream>
#include <fstream>
#include <string>
#include "../include/CacheSimulator.h"

using namespace std;

void printUsage(const string &program_name)
{
    cout << "Usage: " << program_name << " [-t <appname>] [-s <s>] [-E <E>] [-b <b>] [-o <outfilename>]" << endl;
    cout << "Defaults: -t sample, -s 5 (32 sets), -E 4 (4-way), -b 5 (32B block)" << endl;
    cout << "Options:" << endl;
    cout << "  -t <appname>    base name of application traces (default 'sample')" << endl;
    cout << "  -s <s>          number of set index bits, sets = 2^s (default 5)" << endl;
    cout << "  -E <E>          associativity (default 4)" << endl;
    cout << "  -b <b>          number of block offset bits, block bytes = 2^b (default 5)" << endl;
    cout << "  -o <outfilename>: logs output in file for plotting etc." << endl;
    cout << "  -h: prints this help" << endl;
}

int main(int argc, char *argv[])
{
    // Default parameters
    string trace_name = "sample";
    int s = 5;
    int E = 2;
    int b = 6;
    string outfile_name;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i)
    {
        string arg = argv[i];
        if (arg == "-h")
        {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg == "-t" && i + 1 < argc)
        {
            trace_name = argv[++i];
        }
        else if (arg == "-s" && i + 1 < argc)
        {
            s = stoi(argv[++i]);
        }
        else if (arg == "-E" && i + 1 < argc)
        {
            E = stoi(argv[++i]);
        }
        else if (arg == "-b" && i + 1 < argc)
        {
            b = stoi(argv[++i]);
        }
        else if (arg == "-o" && i + 1 < argc)
        {
            outfile_name = argv[++i];
        }
        else
        {
            cerr << "Unknown or incomplete option: " << arg << endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    if (s <= 0 || E <= 0 || b <= 0)
    {
        cerr << "Error: Invalid cache parameters." << endl;
        printUsage(argv[0]);
        return 1;
    }

    CacheSimulator simulator(s, E, b);
    if (!simulator.loadTraces(trace_name))
    {
        cerr << "Error loading trace files." << endl;
        return 1;
    }

    simulator.runSimulation();

    ofstream outfile;
    if (!outfile_name.empty())
    {
        outfile.open(outfile_name);
        if (!outfile.is_open())
        {
            cerr << "Warning: Could not open output file " << outfile_name << endl;
        }
    }

    simulator.printResults(outfile);

    if (outfile.is_open())
        outfile.close();
    return 0;
}
