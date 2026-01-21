# Automated Discovery of Invariant Document Fragments

This repository contains a high-performance implementation of a corpus-driven methodology for the automated discovery of recurring fragments (boilerplate) in large-scale text collections. The system identifies **Maximal Frequent Phrases**: the longest contiguous sequences of tokens that appear across a significant number of unique documents.

## Overview

Modern web corpora may contain significant "template noise" (navigation menus, legal footers, and dynamic UI blocks; often without clear patterns) that inflates index size and diminishes retrieval precision. This tool provides a scalable solution to identify these fragments:

Key points:

* **Dictionary-encoded tokenization** for memory efficiency.
* **Dual-algorithm support**: A novel high-throughput **Bloom-gram** miner 
* **Maximality Guarantee**: Eliminating redundancy to ensure only the most informative, non-overlapping sequences are returned.

**BIDE+** implementation to use as a baseline for comparative analysis.

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