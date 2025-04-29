#include <cstdint>
#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <iomanip>
#include "../include/CacheSimulator.h"

// Define the DEBUG_MODE variable that other files will reference
bool DEBUG_MODE = true;

void printUsage(const std::string &prog)
{
    std::cout << "Usage: " << prog << " [-t <appname>] [-s <s>] [-E <E>] [-b <b>] [-o <outfilename>]\n";
    std::cout << "Defaults: -t sample, -s 5, -E 4, -b 5\n";
}

int main(int argc, char *argv[])
{
    std::string trace_name = "sample";
    int s = 6, E = 2, b = 5;
    std::string outfile_name;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "-h")
        {
            printUsage(argv[0]);
            return 0;
        }
        if (arg == "-t" && i + 1 < argc)
            trace_name = argv[++i];
        else if (arg == "-s" && i + 1 < argc)
            s = std::stoi(argv[++i]);
        else if (arg == "-E" && i + 1 < argc)
            E = std::stoi(argv[++i]);
        else if (arg == "-b" && i + 1 < argc)
            b = std::stoi(argv[++i]);
        else if (arg == "-o" && i + 1 < argc)
            outfile_name = argv[++i];
        else
        {
            std::cerr << "Unknown option " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }
    if (s <= 0 || E <= 0 || b <= 0)
    {
        std::cerr << "Invalid parameters\n";
        return 1;
    }

    if (DEBUG_MODE)
    {
        std::cout << "=== Debug Mode Enabled ===" << std::endl;
        std::cout << "Parameters: s=" << s << ", E=" << E << ", b=" << b << std::endl;
        std::cout << "Trace: " << trace_name << std::endl;
    }

    CacheSimulator sim(s, E, b);
    if (!sim.loadTraces(trace_name))
        return 1;

    sim.runSimulation();

    std::ofstream outfile;
    if (!outfile_name.empty())
        outfile.open(outfile_name);
    sim.printResults(outfile);
    return 0;
}