#include "corpus_miner.h"
#include "signal_handler.h"
#include <filesystem>
#include <iostream>
#include <csignal>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    std::signal(SIGINT, signal_handler);

    if (argc < 2) {
        std::cout << "Usage: ./corpus_miner <dir> [options]\n"
                  << "Options:\n"
                  << "  --n <int>        Min documents (default: 10)\n"
                  << "  --ngrams <int>   N-gram size (default: 4)\n"
                  << "  --mem <int>      Memory limit in MB (0 for no limit)\n"
                  << "  --threads <int>  Max CPU threads (0 for all)\n" << std::endl;
        return 1;
    }

    std::string input_path = argv[1];
    int min_docs = 10;
    int ngrams = 4;
    int mem_limit = 0;
    char csv_delimiter = ','; // Default
    int threads = 0;
    int cache_size = 1000;
    double sampling = 1.0;
    bool in_mem = false;
    bool preload = false;

    std::string mask = "";

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
        }

    std::cout << "[START] Initializing Miner..." << std::endl;
    if (in_mem) std::cout << "[MODE] Running in In-Memory mode (No Disk BIN)" << std::endl;
    CorpusMiner m;
    m.set_limits(threads, mem_limit, cache_size, in_mem, preload);
    m.set_mask(mask);
    if (fs::is_regular_file(input_path)) {
            m.load_csv(input_path, csv_delimiter, sampling);
        } else {
            m.set_mask(mask);
            m.load_directory(input_path, sampling);
        }

    std::cout << "[START] Beginning mining with min_docs=" << min_docs << ", ngrams=" << ngrams << std::endl;
    m.mine(min_docs, ngrams, "results_max.csv");

    std::cout << "[DONE] Process finished." << std::endl;
    return 0;
}
