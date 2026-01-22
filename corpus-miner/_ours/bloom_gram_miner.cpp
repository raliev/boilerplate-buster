#include "bloom_gram_miner.h"
#include "../timer.h"
#include "../signal_handler.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <execution>
#include <queue>
#include <random>
#include <omp.h>
#include <unordered_set>
#include <unordered_map>
#include <memory>
#include <cstring>
#include <vector>
#include <unistd.h>
#ifdef __APPLE__
#include <mach/mach.h>
#endif

namespace fs = std::filesystem;

// Local copy of constants/structs/helpers used by the Bloom n-gram miner.
// These mirror the definitions in corpus_miner.cpp, but are TU-local here.
const int SMALL_NGRAMS_THRESHOLD = 16;  // Use fixed array for n <= this value
const int MAX_NGRAMS_FIXED = 16;        // Maximum size for fixed array
const int DEBUG = 0;                    // to see internal structures in the console

struct RawSeedEntry {
    uint32_t doc_id;
    uint32_t pos;
    int n;

    // Hybrid storage: fixed array for small n-grams, vector for large ones
    union {
        uint32_t fixed_tokens[MAX_NGRAMS_FIXED];
        std::vector<uint32_t>* dynamic_tokens;
    } tokens;

    // Flag to indicate which storage is being used
    bool is_dynamic;

    RawSeedEntry() : doc_id(0), pos(0), n(0), is_dynamic(false) {
        std::memset(tokens.fixed_tokens, 0, sizeof(tokens.fixed_tokens));
    }

    ~RawSeedEntry() {
        if (is_dynamic && tokens.dynamic_tokens != nullptr) {
            delete tokens.dynamic_tokens;
            tokens.dynamic_tokens = nullptr;
        }
    }

    // Copy constructor
    RawSeedEntry(const RawSeedEntry& other)
        : doc_id(other.doc_id),
          pos(other.pos),
          n(other.n),
          is_dynamic(other.is_dynamic) {
        if (is_dynamic) {
            tokens.dynamic_tokens = new std::vector<uint32_t>(*other.tokens.dynamic_tokens);
        } else {
            std::memcpy(tokens.fixed_tokens, other.tokens.fixed_tokens,
                        sizeof(tokens.fixed_tokens));
        }
    }

    // Move constructor
    RawSeedEntry(RawSeedEntry&& other) noexcept
        : doc_id(other.doc_id),
          pos(other.pos),
          n(other.n),
          is_dynamic(other.is_dynamic) {
        if (is_dynamic) {
            tokens.dynamic_tokens = other.tokens.dynamic_tokens;
            other.tokens.dynamic_tokens = nullptr;
        } else {
            std::memcpy(tokens.fixed_tokens, other.tokens.fixed_tokens,
                        sizeof(tokens.fixed_tokens));
        }
    }

    // Copy assignment
    RawSeedEntry& operator=(const RawSeedEntry& other) {
        if (this == &other) return *this;

        // Clean up old data
        if (is_dynamic && tokens.dynamic_tokens != nullptr) {
            delete tokens.dynamic_tokens;
            tokens.dynamic_tokens = nullptr;
        }

        doc_id = other.doc_id;
        pos = other.pos;
        n = other.n;
        is_dynamic = other.is_dynamic;

        if (is_dynamic) {
            tokens.dynamic_tokens = new std::vector<uint32_t>(*other.tokens.dynamic_tokens);
        } else {
            std::memcpy(tokens.fixed_tokens, other.tokens.fixed_tokens,
                        sizeof(tokens.fixed_tokens));
        }
        return *this;
    }

    // Move assignment
    RawSeedEntry& operator=(RawSeedEntry&& other) noexcept {
        if (this == &other) return *this;

        // Clean up old data
        if (is_dynamic && tokens.dynamic_tokens != nullptr) {
            delete tokens.dynamic_tokens;
            tokens.dynamic_tokens = nullptr;
        }

        doc_id = other.doc_id;
        pos = other.pos;
        n = other.n;
        is_dynamic = other.is_dynamic;

        if (is_dynamic) {
            tokens.dynamic_tokens = other.tokens.dynamic_tokens;
            other.tokens.dynamic_tokens = nullptr;
        } else {
            std::memcpy(tokens.fixed_tokens, other.tokens.fixed_tokens,
                        sizeof(tokens.fixed_tokens));
        }
        return *this;
    }

    // Helper to get token at index
    uint32_t get_token(int idx) const {
        if (is_dynamic) {
            return (*tokens.dynamic_tokens)[idx];
        } else {
            return tokens.fixed_tokens[idx];
        }
    }

    // Helper to set token at index
    void set_token(int idx, uint32_t value) {
        if (is_dynamic) {
            (*tokens.dynamic_tokens)[idx] = value;
        } else {
            tokens.fixed_tokens[idx] = value;
        }
    }

    // Initialize with n-grams
    void init_tokens(int num_tokens) {
        if (is_dynamic && tokens.dynamic_tokens != nullptr) {
            delete tokens.dynamic_tokens;
            tokens.dynamic_tokens = nullptr;
        }

        n = num_tokens;
        if (num_tokens > SMALL_NGRAMS_THRESHOLD) {
            is_dynamic = true;
            tokens.dynamic_tokens = new std::vector<uint32_t>(num_tokens, 0);
        } else {
            is_dynamic = false;
            std::memset(tokens.fixed_tokens, 0, sizeof(tokens.fixed_tokens));
        }
    }

    // Оператор для priority_queue (нужен обратный порядок для min-heap)
    bool operator>(const RawSeedEntry& other) const {
        for (int i = 0; i < n; ++i) {
            uint32_t this_token =
                (is_dynamic) ? (*tokens.dynamic_tokens)[i] : tokens.fixed_tokens[i];
            uint32_t other_token = (other.is_dynamic)
                                       ? (*other.tokens.dynamic_tokens)[i]
                                       : other.tokens.fixed_tokens[i];
            if (this_token != other_token) return this_token > other_token;
        }
        if (doc_id != other.doc_id) return doc_id > other.doc_id;
        return pos > other.pos;
    }

    bool same_tokens(const RawSeedEntry& other) const {
        for (int i = 0; i < n; ++i) {
            uint32_t this_token =
                (is_dynamic) ? (*tokens.dynamic_tokens)[i] : tokens.fixed_tokens[i];
            uint32_t other_token = (other.is_dynamic)
                                       ? (*other.tokens.dynamic_tokens)[i]
                                       : other.tokens.fixed_tokens[i];
            if (this_token != other_token) return false;
        }
        return true;
    }

    // Serialization: writes to binary stream
    void write_to_stream(std::ofstream& out) const {
        out.write((char*)&doc_id, sizeof(doc_id));
        out.write((char*)&pos, sizeof(pos));
        out.write((char*)&n, sizeof(n));
        out.write((char*)&is_dynamic, sizeof(is_dynamic));

        if (is_dynamic) {
            for (int i = 0; i < n; ++i) {
                uint32_t token = (*tokens.dynamic_tokens)[i];
                out.write((char*)&token, sizeof(token));
            }
        } else {
            out.write((char*)tokens.fixed_tokens, n * sizeof(uint32_t));
        }
    }

    // Deserialization: reads from binary stream
    void read_from_stream(std::ifstream& in) {
        in.read((char*)&doc_id, sizeof(doc_id));
        in.read((char*)&pos, sizeof(pos));
        in.read((char*)&n, sizeof(n));
        in.read((char*)&is_dynamic, sizeof(is_dynamic));

        if (is_dynamic && tokens.dynamic_tokens != nullptr) {
            delete tokens.dynamic_tokens;
            tokens.dynamic_tokens = nullptr;
        }

        if (is_dynamic) {
            tokens.dynamic_tokens = new std::vector<uint32_t>(n);
            for (int i = 0; i < n; ++i) {
                uint32_t token;
                in.read((char*)&token, sizeof(token));
                (*tokens.dynamic_tokens)[i] = token;
            }
        } else {
            std::memset(tokens.fixed_tokens, 0, sizeof(tokens.fixed_tokens));
            in.read((char*)tokens.fixed_tokens, n * sizeof(uint32_t));
        }
    }
};

inline uint64_t hash_tokens(const uint32_t* tokens, int n) {
    uint64_t h = 14695981039346656037ULL; // FNV offset basis
    for (int i = 0; i < n; ++i) {
        h ^= static_cast<uint64_t>(tokens[i]);
        h *= 1099511628211ULL; // FNV prime
    }
    return h;
}

std::vector<Phrase> BloomNgramMiner::mine(const CorpusMiner& corpus,
                                          const MiningParams& params) {
    // Unpack params
    int min_docs = params.min_docs;
    int ngrams   = params.ngrams;

    // Access corpus-level config/state via getters
    int max_threads        = corpus.get_max_threads();
    size_t memory_limit_mb = corpus.get_memory_limit_mb();
    bool in_memory_only    = corpus.is_in_memory_only();

    const auto& doc_lengths    = corpus.get_doc_lengths();
    const auto& doc_offsets    = corpus.get_doc_offsets();
    const auto& word_df        = corpus.get_word_df();
    const auto& id_to_word     = corpus.get_id_to_word();
    const auto& bin_corpus_path = corpus.get_bin_corpus_path();

    // Local helper to get current RSS (copy of original CorpusMiner::get_current_rss_mb)
    auto get_current_rss_mb = []() -> size_t {
#ifdef __APPLE__
        struct mach_task_basic_info info;
        mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS) {
            return info.resident_size / (1024 * 1024);
        }
        return 0;
#else
        std::ifstream stat_stream("/proc/self/statm", std::ios_base::in);
        unsigned long long pages;
        if (!(stat_stream >> pages >> pages)) return 0;
        return (pages * sysconf(_SC_PAGESIZE)) / (1024 * 1024);
#endif
    };

    if (max_threads > 0) {
        omp_set_num_threads(max_threads);
        std::cout << "[LOG] Threads limited to: " << max_threads << std::endl;
    }

    // 1. Dynamic Filter Size: Aim for ~20% of memory limit, capped at 2GB.
    // A larger filter significantly reduces collisions for bigrams.
    size_t filter_size;
    if (memory_limit_mb > 0) {
        filter_size = (memory_limit_mb * 1024ULL * 1024ULL) / 5; // 20% of limit
        if (filter_size > 2048ULL * 1024ULL * 1024ULL) filter_size = 2048ULL * 1024ULL * 1024ULL;
    } else {
        filter_size = 512ULL * 1024ULL * 1024ULL;
    }

    std::cout << "[LOG] Initializing Bloom Filter: " << (filter_size / (1024 * 1024)) << " MB" << std::endl;
    std::vector<uint8_t> filter_counters(filter_size, 0);

    // Pass 1: Frequency Estimation
    std::cout << "[LOG] Bloom Pass: Estimating n-gram frequencies..." << std::endl;
    #pragma omp parallel
    {
        std::ifstream local_bin;
        if (!in_memory_only) local_bin.open(bin_corpus_path, std::ios::binary);

        #pragma omp for
        for (uint32_t d = 0; d < (uint32_t)doc_lengths.size(); ++d) {
            std::vector<uint32_t> local_doc;
            const std::vector<uint32_t>* doc_ptr = nullptr;

            if (in_memory_only) {
                // In-memory mode: fetch directly from corpus
                doc_ptr = &corpus.get_doc(d);
            } else {
                // Disk mode: load from BIN file
                local_doc.resize(doc_lengths[d]);
                local_bin.seekg(doc_offsets[d]);
                local_bin.read((char*)local_doc.data(), doc_lengths[d] * sizeof(uint32_t));
                doc_ptr = &local_doc;
            }

            if (doc_ptr->size() < (size_t)ngrams) continue;

            // here we count the ngrams before the counter reaches 255
            // the goal is to filter out the ngrams with low frequency (<num_docs) from further processing
            for (uint32_t p = 0; p <= doc_ptr->size() - ngrams; ++p) {
                uint64_t h = hash_tokens(doc_ptr->data() + p, ngrams);
                size_t idx = h % filter_size;

                uint8_t* target = &filter_counters[idx];
                uint8_t current = __atomic_load_n(target, __ATOMIC_RELAXED);
                while (current < 255) {
                    if (__atomic_compare_exchange_n(target, &current, current + 1, false,
                                                    __ATOMIC_RELAXED, __ATOMIC_RELAXED))
                        break;
                }
            }
        }
    }

    // we collected the ngram stats in filter counters
    // we have not saved the ngrams themselves anywhere (because for the large datasets this number can skyrocket)

    // Pass 2: Collection with Statistics
    auto mine_start = start_timer();
    std::cout << "[LOG] Step 1: Gathering " << ngrams << "-gram seeds..." << std::endl;
    auto s1_start = start_timer();
    size_t total_processed = 0;
    size_t seeds_passed = 0;
    size_t seeds_rejected = 0;

    std::string temp_dir = "./miner_tmp";
    fs::create_directories(temp_dir);
    std::vector<std::string> chunk_files;
    std::vector<RawSeedEntry> buffer;
    buffer.reserve(1000000);
    int chunk_id = 0;

    auto flush_buffer = [&]() {
        if (buffer.empty()) return;
        if (in_memory_only) return;
        std::cout << "\n[LOG] Flushing " << buffer.size() << " seeds to disk... (RAM: "
                  << get_current_rss_mb() << " MB)" << std::endl;
        std::sort(std::execution::par, buffer.begin(), buffer.end(),
                  [](const RawSeedEntry& a, const RawSeedEntry& b) {
                      for (int i = 0; i < a.n; ++i) {
                          uint32_t a_token = (a.is_dynamic) ? (*a.tokens.dynamic_tokens)[i]
                                                            : a.tokens.fixed_tokens[i];
                          uint32_t b_token = (b.is_dynamic) ? (*b.tokens.dynamic_tokens)[i]
                                                            : b.tokens.fixed_tokens[i];
                          if (a_token != b_token) return a_token < b_token;
                      }
                      return a.doc_id < b.doc_id;
                  });
        std::string fname = temp_dir + "/chunk_" + std::to_string(chunk_id++) + ".bin";
        chunk_files.push_back(fname);
        std::ofstream out(fname, std::ios::binary);
        if (out) {
            for (const auto& entry : buffer) {
                entry.write_to_stream(out);
            }
        }
        buffer.clear();
        buffer.shrink_to_fit();
    };

    for (uint32_t d = 0; d < (uint32_t)doc_lengths.size(); ++d) {
        // since this is memory intensive processing, we offload data to the files (chunks)
        if (memory_limit_mb > 0 && get_current_rss_mb() >= (size_t)(memory_limit_mb * 0.75))
            flush_buffer();
        // fetch_doc fetches from the disk and caches in memory; with the --in-mem flag it does not use the disk
        const auto& current_doc = corpus.get_doc(d);
        if (current_doc.size() < (size_t)ngrams) continue;

        for (uint32_t p = 0; p <= current_doc.size() - ngrams; ++p) {
            total_processed++;
            uint64_t h = hash_tokens(&current_doc[p], ngrams);

            if (DEBUG) {
                std::cout << "[DEBUG] Doc " << d << " Pos " << p << " Hash: " << h << std::endl;
                std::cout << "[DEBUG] Tokens: ";
                for (int k = 0; k < ngrams; ++k) {
                    std::cout << id_to_word[current_doc[p + k]] << " ";
                }
                std::cout << std::endl;
                std::cout << "[DEBUG] Filter Counter: "
                          << (int)filter_counters[h % filter_size] << std::endl;
                std::cout << std::endl;
                std::cout << std::flush;
            }

            // Bloom Filter check. The BF is probabilistic, it uses a hash as an input which may have collisions
            // we don't process ngrams until they reach min_docs or 255
            if (filter_counters[h % filter_size] >= (uint8_t)std::min(min_docs, 255)) {
                // DF check
                // it is required because Bloom Filter is probabilistic and may produce false positives
                bool df_ok = true;
                for (int i = 0; i < ngrams; ++i) {
                    if (word_df[current_doc[p + i]] < (uint32_t)min_docs) {
                        df_ok = false;
                        break;
                    }
                }

                if (df_ok) {
                    // saving the candidate in the buffer (std::vector<RawSeedEntry>)
                    RawSeedEntry entry;
                    entry.init_tokens(ngrams);
                    entry.n = ngrams;
                    entry.doc_id = d;
                    entry.pos = p;
                    for (int i = 0; i < ngrams; ++i)
                        entry.set_token(i, current_doc[p + i]);
                    buffer.push_back(entry);
                    seeds_passed++;
                } else {
                    seeds_rejected++;
                }
            } else {
                seeds_rejected++;
            }
        }
        if (d % 500 == 0 || d == doc_lengths.size() - 1) {
            std::cout << "[LOG] Scanning: " << (d + 1) << "/" << doc_lengths.size()
                      << " | Seeds Found: " << seeds_passed << " \r" << std::flush;
        }
    }

    // Print Efficiency Statistics
    double efficiency = (total_processed > 0)
                            ? (100.0 * seeds_rejected / total_processed)
                            : 0;
    std::cout << "\n[BLOOM STATS] Total n-grams: " << total_processed << std::endl;
    std::cout << "[BLOOM STATS] Accepted:    " << seeds_passed << std::endl;
    std::cout << "[BLOOM STATS] Rejected:    " << seeds_rejected
              << " (" << efficiency << "% reduction)" << std::endl;

    filter_counters.clear();
    filter_counters.shrink_to_fit();
    if (in_memory_only) {
        std::cout << "[LOG] In-Memory Mode: Sorting all " << buffer.size()
                  << " seeds in RAM..." << std::endl;
        std::sort(std::execution::par, buffer.begin(), buffer.end(),
                  [](const RawSeedEntry& a, const RawSeedEntry& b) {
                      for (int i = 0; i < a.n; ++i) {
                          uint32_t a_token = (a.is_dynamic) ? (*a.tokens.dynamic_tokens)[i]
                                                            : a.tokens.fixed_tokens[i];
                          uint32_t b_token = (b.is_dynamic) ? (*b.tokens.dynamic_tokens)[i]
                                                            : b.tokens.fixed_tokens[i];
                          if (a_token != b_token) return a_token < b_token;
                      }
                      if (a.doc_id != b.doc_id) return a.doc_id < b.doc_id;
                      return a.pos < b.pos;
                  });
    } else {
        flush_buffer();
    }
    std::cout << std::endl;

    // --- START OF STEP 1.5 ---
    std::cout << "[LOG] Step 1.5: Merging and filtering candidates..." << std::endl;
    std::vector<Phrase> candidates;

    if (in_memory_only) {
        // --- PATH A: In-Memory Processing ---
        size_t i = 0;
        while (i < buffer.size()) {
            const RawSeedEntry& representative = buffer[i];
            std::vector<Occurrence> current_occs;
            std::unordered_set<uint32_t> unique_docs;

            // Group identical tokens in the sorted RAM buffer
            while (i < buffer.size() && buffer[i].same_tokens(representative)) {
                current_occs.push_back({buffer[i].doc_id, buffer[i].pos});
                unique_docs.insert(buffer[i].doc_id);
                i++;
            }

            // Support check (min_docs)
            if (unique_docs.size() >= (size_t)min_docs) {
                std::vector<uint32_t> tokens_vec(ngrams);
                for (int k = 0; k < ngrams; ++k) {
                    tokens_vec[k] = (representative.is_dynamic)
                                        ? (*representative.tokens.dynamic_tokens)[k]
                                        : representative.tokens.fixed_tokens[k];
                }
                candidates.push_back(
                    {tokens_vec, std::move(current_occs), unique_docs.size()});
            }
        }
        // Free RAM immediately
        buffer.clear();
        buffer.shrink_to_fit();
    } else {
        // --- PATH B: Disk-Based External Merge ---
        struct ChunkReader {
            std::ifstream stream;
            RawSeedEntry current;
            bool active;
            bool next() {
                try {
                    current.read_from_stream(stream);
                    if (!stream) {
                        active = false;
                        return false;
                    }
                    return true;
                } catch (...) {
                    active = false;
                    return false;
                }
            }
        };

        auto cmp = [](ChunkReader* a, ChunkReader* b) { return a->current > b->current; };
        std::priority_queue<ChunkReader*, std::vector<ChunkReader*>, decltype(cmp)> pq(cmp);
        std::vector<std::unique_ptr<ChunkReader>> readers;

        for (const auto& file : chunk_files) {
            auto r = std::make_unique<ChunkReader>();
            r->stream.open(file, std::ios::binary);
            if (r->next()) {
                r->active = true;
                pq.push(r.get());
            }
            readers.push_back(std::move(r));
        }

        while (!pq.empty()) {
            RawSeedEntry representative = pq.top()->current;
            std::vector<Occurrence> current_occs;
            std::unordered_set<uint32_t> unique_docs;

            while (!pq.empty() && pq.top()->current.same_tokens(representative)) {
                ChunkReader* r = pq.top();
                pq.pop();

                current_occs.push_back({r->current.doc_id, r->current.pos});
                unique_docs.insert(r->current.doc_id);

                if (r->next()) pq.push(r);
            }

            if (unique_docs.size() >= (size_t)min_docs) {
                std::vector<uint32_t> tokens_vec(ngrams);
                for (int k = 0; k < ngrams; ++k) {
                    tokens_vec[k] = (representative.is_dynamic)
                                        ? (*representative.tokens.dynamic_tokens)[k]
                                        : representative.tokens.fixed_tokens[k];
                }
                candidates.push_back(
                    {tokens_vec, std::move(current_occs), unique_docs.size()});
            }
        }

        // Cleanup Disk Resources
        for (auto& r : readers) {
            if (r->stream.is_open()) r->stream.close();
        }
        readers.clear();

        try {
            if (fs::exists(temp_dir)) {
                fs::remove_all(temp_dir);
                std::cout << "[LOG] Step 1.5: Temporary directory and chunk files removed."
                          << std::endl;
            }
        } catch (const fs::filesystem_error& e) {
            std::cerr << "[WARNING] Cleanup failed: " << e.what() << std::endl;
        }
    }
    // --- END OF STEP 1.5 ---

    size_t total_seeds_generated = candidates.size();
    stop_timer(std::to_string(ngrams) + "-gram Seed Generation (Disk)", s1_start);

    std::cout << "[LOG] Step 2: Sorting " << candidates.size()
              << " candidates by score (support * length)..." << std::endl;

    std::sort(std::execution::par, candidates.begin(), candidates.end(),
              [](const Phrase& a, const Phrase& b) {
                  size_t score_a = a.support * a.tokens.size();
                  size_t score_b = b.support * b.tokens.size();

                  if (score_a != score_b) {
                      return score_a > score_b; // Сначала те, что покрывают больше текста
                  }
                  return a.support > b.support; // При равном весе — те, что чаще
              });

    std::cout << "[LOG] Step 3: Expanding with Path Compression (Jumps)..." << std::endl;
    auto s3_start = start_timer();
    std::vector<Phrase> final_phrases;

    std::vector<std::vector<bool>> processed(doc_lengths.size());
    for (size_t i = 0; i < doc_lengths.size(); ++i) {
        processed[i].assign(doc_lengths[i], false);
    }

    for (size_t c_idx = 0; c_idx < candidates.size(); ++c_idx) {
        if (g_stop_requested) {
            std::cout << "\n[!] Expansion interrupted. Moving to save results..."
                      << std::endl;
            break;
        }

        if (c_idx % 100 == 0 || c_idx == candidates.size() - 1) {
            std::cout << "[LOG] Expanding: " << (c_idx + 1) << "/" << candidates.size()
                      << " | Phrases found: " << final_phrases.size()
                      << "          \r" << std::flush;
        }

        auto& cand = candidates[c_idx];

        bool all_processed = true;
        for (auto& o : cand.occs) {
            if (!processed[o.doc_id][o.pos]) {
                all_processed = false;
                break;
            }
        }
        if (all_processed) continue;

        while (true) {
            std::unordered_map<uint32_t, std::vector<Occurrence>> next_word_occs;
            for (auto& o : cand.occs) {
                const auto& doc = corpus.get_doc(o.doc_id);
                uint32_t np = o.pos + (uint32_t)cand.tokens.size();
                if (np < doc.size()) {
                    next_word_occs[doc[np]].push_back(o);
                }
            }

            uint32_t best_word = 0;
            size_t max_support = 0;
            std::vector<Occurrence> best_next_occs;

            for (auto& [word, occs] : next_word_occs) {
                std::unordered_set<uint32_t> unique_docs;
                for (auto& o : occs) unique_docs.insert(o.doc_id);

                if (unique_docs.size() >= (size_t)min_docs &&
                    unique_docs.size() >= max_support) {
                    max_support = unique_docs.size();
                    best_word = word;
                    best_next_occs = std::move(occs);
                }
            }

            if (max_support > 0) {
                cand.tokens.push_back(best_word);
                cand.occs = std::move(best_next_occs);
                cand.support = max_support;
            } else break;
        }

        if (!cand.occs.empty()) {
                    bool has_common_prefix = false;
                    uint32_t first_doc = cand.occs[0].doc_id;
                    int first_pos = (int)cand.occs[0].pos;

                    if (first_pos > 0) {
                        uint32_t common_prev = corpus.get_doc(first_doc)[first_pos - 1];
                        bool all_match = true;
                        for (const auto& o : cand.occs) {
                            if (o.pos == 0 || corpus.get_doc(o.doc_id)[o.pos - 1] != common_prev) {
                                all_match = false;
                                break;
                            }
                        }
                        if (all_match) has_common_prefix = true;
                    }

                    if (has_common_prefix) {
                        continue; // Skip this phrase: it is not backward-closed
                    }
                }

        for (auto& o : cand.occs) {
            for (uint32_t i = 0; i < (uint32_t)cand.tokens.size(); ++i) {
                if (o.pos + i < processed[o.doc_id].size())
                    processed[o.doc_id][o.pos + i] = true;
            }
        }
        if (cand.tokens.size() >= (size_t)params.min_l) {
             final_phrases.push_back(std::move(cand));
         }
    }
    std::cout << std::endl;
    stop_timer("Expansion & Pruning", s3_start);

    size_t count_6plus = 0;
    for (const auto& p : final_phrases)
        if (p.tokens.size() >= 6) count_6plus++;

    std::cout << "\n========== MINING STATISTICS ==========" << std::endl;
    std::cout << "Candidates after merge:       " << total_seeds_generated << std::endl;
    std::cout << "Total phrases mined:          " << final_phrases.size() << std::endl;
    std::cout << "Long phrases (6+ words):      " << count_6plus << std::endl;
    std::cout << "=======================================\n" << std::endl;

    stop_timer("Total Mining Process", mine_start);

    return final_phrases;
}
