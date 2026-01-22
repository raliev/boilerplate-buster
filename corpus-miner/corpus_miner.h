// c_src/corpus_miner.h
#ifndef CORPUS_MINER_H
#define CORPUS_MINER_H

#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include "types.h"

// Forward declaration for algorithms
class IMiningAlgorithm;

class CorpusMiner {
private:
    std::vector<std::string> id_to_word;
    std::unordered_map<std::string, uint32_t> word_to_id;
    std::vector<uint32_t> word_df;
    std::vector<std::vector<uint32_t>> docs;
    std::vector<std::string> file_paths;
    std::mutex dict_mtx;

    int max_threads = 0;
    int min_tokens = 0;
    size_t memory_limit_mb = 0;

    std::string file_mask = "";

    size_t get_current_rss_mb();

    std::string bin_corpus_path = "corpus_data.bin";
    std::vector<size_t> doc_offsets;
    std::vector<uint32_t> doc_lengths;

    mutable std::mutex cache_mtx;
    mutable std::unordered_map<uint32_t, std::vector<uint32_t>> doc_cache;

    bool in_memory_only = false;
    bool preload_cache = false;
    size_t max_cache_size = 1000;

    const std::vector<uint32_t>& fetch_doc(uint32_t doc_id) const;

    void export_to_spmf(const std::string& path) const;
    void import_from_spmf(const std::string& spmf_out, const std::string& final_csv, int min_l);


public:

    void run_spmf(const std::string& algo,
              const std::string& spmf_params,
              const std::string& jar_path,
              int min_docs,
              const std::string& output_csv,
              int min_l);

    void set_mask(const std::string& mask) { file_mask = mask; }

    void set_limits(int threads, size_t mem_mb, size_t cache_size, bool in_mem, bool preload, int min_l) {
        max_threads    = threads;
        memory_limit_mb = mem_mb;
        max_cache_size  = cache_size;
        in_memory_only  = in_mem;
        preload_cache   = preload;
        min_tokens = min_l;
    }

    int get_max_threads() const { return max_threads; }
    size_t get_memory_limit_mb() const { return memory_limit_mb; }
    bool is_in_memory_only() const { return in_memory_only; }
    size_t get_max_cache_size() const { return max_cache_size; }

    size_t num_docs() const { return doc_lengths.size(); }

    const std::vector<uint32_t>& get_doc(uint32_t doc_id) const {
        return fetch_doc(doc_id);
    }

    const std::vector<uint32_t>& get_doc_lengths() const { return doc_lengths; }
    const std::vector<size_t>& get_doc_offsets() const { return doc_offsets; }

    const std::vector<std::string>& get_id_to_word() const { return id_to_word; }
    const std::vector<uint32_t>& get_word_df() const { return word_df; }
    const std::vector<std::string>& get_file_paths() const { return file_paths; }
    const std::string& get_bin_corpus_path() const { return bin_corpus_path; }

    void load_directory(const std::string& path, double sampling = 1.0);
    void load_csv(const std::string& path, char delimiter = ',', double sampling = 1.0);

    void save_to_csv(const std::vector<Phrase>& res, const std::string& out_p);
};

#endif // CORPUS_MINER_H