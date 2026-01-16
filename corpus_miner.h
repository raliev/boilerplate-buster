#ifndef CORPUS_MINER_H
#define CORPUS_MINER_H

#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include "types.h"

class CorpusMiner {
private:
    std::vector<std::string> id_to_word;
    std::unordered_map<std::string, uint32_t> word_to_id;
    std::vector<uint32_t> word_df;
    std::vector<std::vector<uint32_t>> docs;
    std::vector<std::string> file_paths;
    std::mutex dict_mtx;

public:
    // Default value stays ONLY here
    void load_directory(const std::string& path, double sampling = 1.0);
    void mine(int min_docs, int ngrams, const std::string& output_csv);
    void save_to_csv(const std::vector<Phrase>& res, const std::string& out_p);
};

#endif