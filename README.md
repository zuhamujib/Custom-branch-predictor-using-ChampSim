# Custom-TAGE-inspired-branch-predictor-using-ChampSim
_**ECE552 COMPUTER ARCHITECTURE | University of Toronto**_

_This project was developed as part of the ECE552 Computer Architecture course at the University of Toronto. The assignment specifications and provided materials are the property of the course teaching team. The implementation in this repository is my own work._

A C++ implementation of a two-bit saturating counter baseline and a
custom TAGE-inspired branch predictor using ChampSim. Evaluated across
eight benchmark workloads, the custom predictor achieved a geometric
mean MPKI of 1.446, compared with 3.770 for the baseline that is a
61.6% reduction was achieved.

### Overview
Branch prediction helps modern processors reduce control hazards by predicting the outcome of branch instructions before they are resolved. This project implements and evaluates two dynamic branch predictors in ChampSim, a trace-based microarchitecture simulator:
* SBitSat: A two-bit saturating counter baseline.
* Custom-TAGE: A TAGE-inspired predictor that combines a PC-indexed base table with tagged tables using progressively longer global branch histories.

### Predictor Architecture
The custom predictor uses branch history at multiple time scales to identify recurring patterns and improve prediction accuracy. Predictor Architecture. The custom predictor consists of a 2,048-entry base prediction table and six tagged tables, each containing 512 entries.

#### Component Configuration
 Base prediction table | 2,048 entries 
 --- | ---
 Tagged tables | 6 × 512 entries 
 Global history lengths | 4, 8, 16, 32, 64, 128 
 Tag width | 10 bits 
 Prediction counter | 2 bits per entry 
 Usefulness counter | 2 bits per entry 

A history-dependent hashing function combines the branch program counter
(PC) with recent global branch outcomes to generate table indices and
tags. During prediction, the tagged tables are searched from shortest to
longest history. The longest-history table with a matching tag provides
the prediction; if no tag matches, the base predictor is used.

### Prediction and Training
Each prediction counter is a two-bit saturating counter with four
states, from strongly not taken to strongly taken. Once the actual
branch outcome is known, the selected counter is updated toward that
outcome.

Usefulness counters track whether a tagged-table prediction is more
reliable than the alternate prediction when the two disagree. Following
a misprediction, the predictor attempts to allocate an entry in a
longer-history table. Allocation is limited to one entry per
misprediction, and entries with zero usefulness are eligible for
replacement. Global history is updated after prediction and training.

### Storage Budget
The custom predictor requires 47,232 logical bits (46.125 Kibibits),
excluding indexing and control logic.

| Storage component | Bits |
--- | ---
| Base prediction counters | 4,096 |
| Tagged prediction counters | 6,144 |
| Usefulness counters | 6,144 |
| Tags | 30,720 |
| Global history | 128 |
| Total | 47,232 |

This is a logical hardware storage estimate, not the memory footprint of
the corresponding C++ data structures.

### Evaluation and Results
Both predictors were evaluated using ChampSim simulation traces from
eight benchmark workloads provided by the teaching team. Performance was
measured using mispredictions per thousand instructions (MPKI), with
geometric mean MPKI used to compare results across the workload suite.

Predictor | Geometric mean MPKI
  --- | ---
Two-bit saturating counter (2BitSat) | 3.770 
Custom-TAGE | 1.446

The custom predictor reduced geometric mean MPKI by approximately
61.6% compared with the two-bit baseline.

### Implementation
- Implemented the SBitSat baseline predictor in C++ within ChampSim.
- Designed the custom TAGE-inspired predictor with a base table and
  six tagged tables.
- Implemented PC- and history-dependent hashing for tagged-table
  indexing and tag generation.
- Added prediction and usefulness counters, global history tracking,
  and selective longer-history entry allocation.
- Calculated the predictor's logical storage requirement and evaluated
  performance across eight benchmark workloads.

### Technologies
- C++
- ChampSim
- Computer Architecture
- Dynamic Branch Prediction
- TAGE-inspired Prediction
- Global Branch History
- Benchmarking and MPKI Analysis

