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

    std::string file_mask = "";

    // Вспомогательный метод для получения текущего потребления RAM (RSS)
    size_t get_current_rss_mb();

    std::string bin_corpus_path = "corpus_data.bin";
    std::vector<size_t> doc_offsets;
    std::vector<uint32_t> doc_lengths;

    // Simple Cache: doc_id -> token vector
    mutable std::mutex cache_mtx;
    mutable std::unordered_map<uint32_t, std::vector<uint32_t>> doc_cache;

    bool in_memory_only = false;
    bool preload_cache = false;
    size_t max_cache_size = 1000;

    const std::vector<uint32_t>& fetch_doc(uint32_t doc_id) const;

public:
    void set_mask(const std::string& mask) { file_mask = mask; }
    void set_limits(int threads, size_t mem_mb, size_t cache_size, bool in_mem, bool preload) {
            max_threads = threads;
            memory_limit_mb = mem_mb;
            max_cache_size = cache_size;
            in_memory_only = in_mem;
            preload_cache = preload;
        }

    void load_directory(const std::string& path, double sampling = 1.0);
    void mine(int min_docs, int ngrams, const std::string& output_csv);
    void save_to_csv(const std::vector<Phrase>& res, const std::string& out_p);
};

#endif