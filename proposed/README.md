# Automated Discovery of Invariant Document Fragments

This repository contains a high-performance implementation of a corpus-driven methodology for the automated discovery of recurring fragments (boilerplate) in large-scale text collections. The system identifies **Maximal Frequent Phrases**: the longest contiguous sequences of tokens that appear across a significant number of unique documents.

## Overview

Modern web corpora contain significant "template noise" (navigation menus, legal footers, and dynamic UI blocks) that inflates index size and diminishes retrieval precision. This tool provides a scalable solution to identify these fragments by:

* Employing dictionary-encoded tokenization.
* Utilizing a bottom-up iterative expansion algorithm.
* Applying a scoring function $\Psi(P) = |P| \times df(P)$ to prioritize the most informative sequences.
* Using a global occupancy bitmask to eliminate redundancy and ensure phrase maximality.


## Architecture and Components

The project is structured into two distinct layers to balance performance and usability:


### 1. C++ Core Engine

The high-performance module responsible for heavy computational tasks:

* **Tokenization and Encoding:** Rapid conversion of raw text into 32-bit integer sequences.
* **Frequency Estimation:** Leveraging counting Bloom Filters and parallel sorting to identify frequent n-grams.
* **Phrase Expansion:** Multi-threaded logic that grows initial "seeds" into maximal phrases using OpenMP.

### 2. Python script for visualizing CSV files


## Configuration and CLI Flags

### C++ Engine Parameters

These flags control the low-level mining logic:

* --input / -i: Path to the directory containing the text corpus.
* --output / -o: Path to the resulting CSV file containing extracted fragments.
* --sigma / -s: **Minimum Support Threshold.** The minimum number of *unique documents* required for a phrase to be considered frequent. High values filter out noise; low values capture more specific templates.
* --seed / -n: **Initial Seed Length.** The starting length for n-gram candidates (e.g., 5 tokens). This prevents the engine from wasting cycles on trivial short phrases.
* --threads / -t: Number of OpenMP threads. Defaults to the maximum available hardware threads.



## Requirements


* **C++:** A compiler with C++20 support.
* **Python:** Version 3.12 or higher.
* **Libraries:**
    * **Intel TBB:** Required for C++20 parallel execution policies.
    * **OpenMP:** Required for multi-threaded document processing.

### macOS Installation

Since the default Apple Clang does not support OpenMP, it is recommended to use GCC via Homebrew:

brew install gcc tbb \


## Methodology

The discovery process follows four distinct phases:

1. **Preprocessing:** Parallel tokenization and integer dictionary encoding.
2. **Probabilistic Estimation:** Frequency estimation via counting Bloom Filters to prune the search space with minimal memory overhead.
3. **Seed Generation:** Gathering validated n-grams using an external-memory sort-merge strategy.
4. **Priority-Based Expansion:** A greedy heuristic with path compression that extends seeds until they no longer meet the support threshold $\sigma$, while using an occupancy mask to skip redundant sub-sequences.


## Performance (Gutenberg Benchmark)

* **Dataset Size:** 3GB (8,544 documents).
* **Processing Time:** ~113 seconds total.
* **Throughput:** Seed generation in 17s; expansion and pruning in 116s.
* **Output:** Successfully extracted over 15,000 unique maximal frequent phrases.

## Author

Rauf Aliev

Independent Researcher

Email: r.aliev@gmail.com
