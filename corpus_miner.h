// c_src/corpus_miner.h
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

    // Новые параметры лимитов
    int max_threads = 0;
    size_t memory_limit_mb = 0;

    // Вспомогательный метод для получения текущего потребления RAM (RSS)
    size_t get_current_rss_mb();

public:
    void set_limits(int threads, size_t mem_mb) {
        max_threads = threads;
        memory_limit_mb = mem_mb;
    }

    void load_directory(const std::string& path, double sampling = 1.0);
    void mine(int min_docs, int ngrams, const std::string& output_csv);
    void save_to_csv(const std::vector<Phrase>& res, const std::string& out_p);
};

#endif