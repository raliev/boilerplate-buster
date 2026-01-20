#include "corpus_miner.h"
#include "signal_handler.h"
#include "algorithm_factory.h"
#include <filesystem>
#include <iostream>
#include <csignal>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    std::signal(SIGINT, signal_handler);

    if (argc < 2) {
        std::cout << "Usage: ./corpus_miner <dir-or-csv> [options]\n"
                  << "Options:\n"
                  << "  --n <int>        Min documents (default: 10)\n"
                  << "  --ngrams <int>   N-gram size (default: 4)\n"
                  << "  --mem <int>      Memory limit in MB (0 for no limit)\n"
                  << "  --threads <int>  Max CPU threads (0 for all)\n"
                  << "  --algo <name>    Mining algorithm (default: bloom)\n"
                  << std::endl;
        return 1;
    }

    std::string input_path = argv[1];
    int min_docs = 10;
    int ngrams   = 4;
    int mem_limit = 0;
    char csv_delimiter = ',';
    int threads = 0;
    int cache_size = 1000;
    double sampling = 1.0;
    bool in_mem = false;
    bool preload = false;
    std::string mask = "";

    std::string algo_name = "bloom";   // NEW default

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--n" && i + 1 < argc) min_docs = std::stoi(argv[++i]);
        else if (arg == "--mask" && i + 1 < argc) mask = argv[++i];
        else if (arg == "--ngrams" && i + 1 < argc) ngrams = std::stoi(argv[++i]);
        else if (arg == "--csv-delimiter" && i + 1 < argc) {
            std::string delim = argv[++i];
            if (delim == "\\t") csv_delimiter = '\t';
            else if (delim == "\\n") csv_delimiter = '\n';
            else if (!delim.empty()) csv_delimiter = delim[0];
        }
        else if (arg == "--mem" && i + 1 < argc) mem_limit = std::stoi(argv[++i]);
        else if (arg == "--threads" && i + 1 < argc) threads = std::stoi(argv[++i]);
        else if (arg == "--sampling" && i + 1 < argc) sampling = std::stod(argv[++i]);
        else if (arg == "--cache" && i + 1 < argc) cache_size = std::stoi(argv[++i]);
        else if (arg == "--in-mem") in_mem = true;
        else if (arg == "--preload") preload = true;
        else if (arg == "--algo" && i + 1 < argc) algo_name = argv[++i];   // NEW
    }

    std::cout << "[START] Initializing Miner..." << std::endl;
    if (in_mem) std::cout << "[MODE] Running in In-Memory mode (No Disk BIN)" << std::endl;

    CorpusMiner corpus;
    corpus.set_limits(threads, mem_limit, cache_size, in_mem, preload);
    corpus.set_mask(mask);

    if (fs::is_regular_file(input_path)) {
        corpus.load_csv(input_path, csv_delimiter, sampling);
    } else {
        corpus.load_directory(input_path, sampling);
    }

    // NEW: Select algorithm and run it
    AlgorithmKind kind = parse_algorithm_kind(algo_name);
    auto algo = make_algorithm(kind);

    MiningParams params{min_docs, ngrams, "results_max.csv"};

    std::cout << "[START] Beginning mining with algorithm=" << algo->name()
              << ", min_docs=" << min_docs << ", ngrams=" << ngrams << std::endl;

    std::vector<Phrase> phrases = algo->mine(corpus, params);

    corpus.save_to_csv(phrases, params.output_csv);

    std::cout << "[DONE] Process finished." << std::endl;
    return 0;
}