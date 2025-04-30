# Cache Simulator README

This README describes how to build and run the MESI-based cache simulator (L1simulate).

---

## Prerequisites

- A C++17-compatible compiler (e.g., `g++`, `clang++`).
- GNU Make.
- Linux/macOS terminal (or equivalent shell).

---

## Project Structure

```
project-root/
├── include/            # Header files (.h)
├── src/                # Source files (.cpp)
├── obj/                # Build artifacts (auto-generated)
├── Makefile            # Build configuration
└── README.md           # This file
```

---

## Makefile Overview

- **Compiler**: `CC = g++` (modifiable).
- **Flags**: `CFLAGS = -std=c++17 -g -Wall -O3`.
- **Source directory**: `SRC_DIR = src`.
- **Object directory**: `OBJ_DIR = obj`.
- **Target executable**: `TARGET = L1simulate`.

Key targets:

- **`make`** (default): Compiles all sources and produces `L1simulate`.
- **`make clean`**: Removes `L1simulate`, all `.o` files, and `obj/` directory.

---

## Building the Simulator

### Using Makefile

```bash
make
```

- Creates `obj/` if missing.
- Compiles each `.cpp` under `src/` into `obj/*.o`.
- Links all object files into the executable `L1simulate`.

---

## Running the Simulator

Usage:

```bash
./L1simulate [-t <tracefile>] [-s <s>] [-E <E>] [-b <b>] [-o <outfilename>] [-h]
```

Where:

- `-t <tracefile>`: Base name of traces (default: `sample`).
- `-s <s>`: Number of set-index bits (default: `6`).
- `-E <E>`: Associativity (default: `2`).
- `-b <b>`: Number of block-offset bits (default: `5`).
- `-o <outfilename>`: (Optional) Write detailed CSV output to file.
- `-h`: Show help and exit.

The simulator will look for:

```
<tracefile>_proc0.trace
<tracefile>_proc1.trace
... up to number of cores.
```

### Example

```bash
make
./L1simulate -t traces/appX -s 4 -E 2 -b 4 -o results.csv
```

---

## Trace File Format

Each line:

```
R 0x1A2B3C4D  # Read at hex address
W 0x00FF7700  # Write at hex address
```

- `R` or `W`: Operation type.
- `0x...`: 32-bit hex address.

---

## Output

- **Console**: Summary of parameters, per-core stats, overall bus summary.
- **CSV**: When using `-o`, the file contains comma-separated stats for each core and bus.

---

## Debug Mode

To enable cycle-by-cycle logs and bus-queue dumps, recompile with `DEBUG_MODE = true` in `main.cpp` (or pass `--debug` if implemented) and run again.\
This prints detailed messages for each cycle.

---

## Cleaning Up

```bash
make clean
```

Removes the executable, object files, and `obj/` directory.

