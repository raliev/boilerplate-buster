# Automated Discovery of Invariant Document Fragments

This repository contains a high-performance implementation of a corpus-driven methodology for the automated discovery of recurring fragments (boilerplate) in large-scale text collections. The system identifies **Maximal Frequent Phrases**: the longest contiguous sequences of tokens that appear across a significant number of unique documents.

## Overview

Modern web corpora may contain significant "template noise" (navigation menus, legal footers, and dynamic UI blocks; often without clear patterns) that inflates index size and diminishes retrieval precision. This tool provides a scalable solution to identify these fragments.

The proposed algorithm architecture facilitates an efficient, multi-stage pipeline for the extraction of maximal frequent phrases from a discrete corpus $\mathcal{C}$ by synthesizing multi-modal storage strategies with a priority-based greedy expansion heuristic. The process is initiated through parallelized tokenization and 32-bit integer encoding via the OpenMP framework, which establishes a global vocabulary $\Sigma$ while simultaneously calculating individual word document frequencies ($df$). This preliminary analysis allows for the immediate pruning of sequences containing tokens that fail to satisfy the minimum support threshold $\sigma$. To further optimize the search space, a probabilistic estimation phase is implemented using a counting Bloom Filter. By mapping $n$-grams to a set of atomic 8-bit counters $\mathcal{B}$ via an FNV-1a hash function, the system can efficiently identify and discard infrequent candidates with minimal memory overhead before they enter the sorting stage.

Initial $n$-gram seed generation is managed through a dual-path execution strategy that adapts to the available hardware constraints. Depending on the specified memory limits, the system either employs an external-memory sort-merge strategy---utilizing lexicographically sorted binary chunks and a $k$-way priority merge---or executes a high-speed parallel sort of the seed buffer entirely within RAM to eliminate disk I/O latency. Once validated, these candidates are ranked by a textual coverage scoring function $\Psi(P) = |P| \cdot df(P)$, which prioritizes phrases covering the largest cumulative "text area".

The final extraction phase utilizes a greedy expansion heuristic with path compression. For each candidate, the algorithm evaluates all possible forward-adjacent tokens and appends the specific token $t^*$ that yields the highest unique document support. To ensure the property of maximality and prevent the extraction of redundant sub-sequences, a global bitmask $\mathcal{M}$ tracks the processed status of every token position in the corpus. This ensures that once a dominant phrase is finalized, its constituent tokens are marked to suppress the output of lower-scoring fragments.

**BIDE+** implementation to use as a baseline for comparative analysis.

## Getting Started

```bash
cd corpus_miner
make
./corpus_miner ../tests/test1 --ngrams 3 --n 3 --algo bloomspan
cat results_max.csv
python3 process_results_csv.py --min_l=3
open visualization.html
```


## Algorithms

The framework supports multiple mining strategies depending on the research objective:

### 1. Primary Algorithm: BloomSpan Miner (Default)

The native "Default" mode is an optimized algorithm designed for extracting contiguous frequent phrases. It uses a **Counting Bloom Filter** to estimate n-gram frequencies with a very small memory footprint, followed by a priority-based expansion using the scoring function $\Psi(P) = |P| \times df(P)$.

* **Usage**: Run without `--algo` or with `--algo bloomspan`.

### 2. BIDE+ Miner
A high-performance C++ implementation of the **BIDE+** (Bidirectional Extension) algorithm. Unlike the default miner, BIDE+ is designed to find **Closed Sequential Patterns**, ensuring that a frequent phrase is only reported if it cannot be extended in either direction without losing support.

* **Usage**: Use the flag `--algo bide`.

### 3. SPMF Integration (Benchmarking)
The framework includes a wrapper for the **SPMF (Sequential Pattern Mining Framework)** library. This is intended solely for **comparative analysis and testing**.

* **Setup**: Download `spmf.jar` from the [official site](http://www.philippe-fournier-viger.com/spmf/) and place it in the same directory as the `corpus_miner` binary.
* **Usage**: Use the flag `--spmf` and `--spmf-params "<params>"` and `--spmf-location /path/to/spmf.jar`.

Вот перевод описания новизны вашего алгоритма на английский язык, а также готовая секция для вашего README.md.

Novelty of the BloomNgramMiner Algorithm
The novelty of the BloomNgramMiner lies in its unique combination of probabilistic data structures, out-of-core processing, and a greedy expansion strategy designed specifically for large-scale text corpora.

📝 README Section (Markdown)
Markdown

## Algorithm Novelty: BloomSpan vs BIDE+

The `BloomSpan` is a high-performance sequential phrase discovery algorithm designed to handle datasets that exceed available RAM. It introduces several key innovations:

### 1. Probabilistic Frequency Estimation (Bloom Pass)
Unlike traditional miners that store all n-gram candidates, this algorithm performs a **pre-emptive frequency estimation**:
* **Bloom Filter with Counters**: It uses a thread-safe Bloom Filter to estimate n-gram frequencies in a single pass.
* **Noise Reduction**: N-grams with a frequency lower than the `min_docs` threshold are discarded immediately, preventing memory explosion from rare sequences.

### 2. Greedy Expansion with Path Compression
The core mining logic employs a "Seed-and-Expand" strategy with significant optimizations:
* **Jumps**: Starting from an n-gram seed, the algorithm greedily expands to the right by selecting the most frequent subsequent tokens.
* **Global Pruning**: A bit-matrix (or vector of booleans) tracks already processed positions in the corpus. Once a long phrase is found, its constituent tokens are marked, preventing the redundant discovery of sub-phrases.

### 3. Hybrid Memory-Efficient Storage (optional)
The algorithm utilizes a custom `RawSeedEntry` structure to optimize memory footprint:
* **Fixed vs. Dynamic**: It uses a fixed-size array for short n-grams (up to 16 tokens) to avoid frequent heap allocations.
* **Switching Logic**: It transparently switches to dynamic vector storage only when the sequence length exceeds the threshold.

The miner is built for "Big Data" scenarios through a robust disk-based architecture:
* **External Merge Sort**: When RAM usage reaches a defined limit, the algorithm flushes sorted "chunks" of candidates to disk.
* **Priority Queue Merging**: It reconstructs the final candidate list using a disk-aware merge sort, allowing it to process corpora of virtually any size.

### 4. Multi-threaded Score Prioritization
Before expansion, candidates are prioritized based on a scoring function: $Score = Support \times Length$. This ensures that the most "descriptive" and heavy-weight phrases are processed first, maximizing the efficiency of the pruning bit-matrix.

## Configuration and CLI Flags

### C++ Core Engine Parameters

the first parameter is a directory or a CSV file. In case of a directory, all files are read, recursively. In case of CSV file, all rows are considered to be "documents".

Optional parameters:

* `--n`: **Minimum Support Threshold.** The minimum number of *unique documents* required for a phrase to be considered frequent. This is not a percentage, it is an absolute value of the number of documents.
* `--ngrams`: **Minimum Phrase Length.** Filters out trivial short sequences (e.g., set to 5+ for boilerplate).
* `--algo`: Algorithm selection (`bloom` (default), `bide`).
* `--threads`: To limit the number of OpenMP threads (defaults to hardware maximum; used only by the default algorithm, bloomspan).
* `--mem`: To limit the memory use by a ngram builder (used only by the default algorithm, bloomspan).

## Synthetic Data & Evaluation

To support scientific validation of precision and recall, the repository includes specialized utility scripts:

### Dataset Generator

Use `generate_test_dataset.py` to create a synthetic corpus with embedded "golden" patterns. This allows researchers to verify if the algorithms can recover known fragments at specific frequency and length thresholds.
```bash
python3 generate_test_dataset.py
```

It creates 100,000 documents in the ..`/test/generated` folder. Each document has 500 unique, synthetic words (which makes a dictionary of 50M unique words). It also injects the phrases into random documents according to the injection rules listed in the `generate_test_dataset.csv`:

```csv
"This is the first test sentence",3
"Another unique phrase for testing",5
"A third sentence",4
```

It is expected that running `./corpus_miner ../tests/generated --ngrams 3 --n 3` should create a file `results_max.csv` of the following structure:

```csv
phrase,freq,length,example_files
"another unique phrase for testing",5,5,"../tests/generated/test_file_74385.txt|../tests/generated/test_file_82694.txt"
"a third sentence",4,3,"../tests/generated/test_file_10549.txt|../tests/generated/test_file_36741.txt"
"this is the first test sentence",3,6,"../tests/generated/test_file_67301.txt|../tests/generated/test_file_91313.txt"
```