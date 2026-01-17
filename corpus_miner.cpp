#include "corpus_miner.h"
#include "tokenizer.h"
#include "timer.h"
#include "signal_handler.h"
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <filesystem>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <thread>
#include <mutex>
#include <random>
#include <execution>
#include <omp.h>
#include <queue>
#include <memory>
#include <cstring>
#include <vector>

namespace fs = std::filesystem;

struct RawSeedEntry {
    uint32_t tokens[10]; // Поддержка до 10-грамм (хватит для большинства задач)
    uint32_t doc_id;
    uint32_t pos;
    int n; // текущий размер ngrams

    // Оператор для priority_queue (нужен обратный порядок для min-heap)
    bool operator>(const RawSeedEntry& other) const {
        for (int i = 0; i < n; ++i) {
            if (tokens[i] != other.tokens[i]) return tokens[i] > other.tokens[i];
        }
        if (doc_id != other.doc_id) return doc_id > other.doc_id;
        return pos > other.pos;
    }

    bool same_tokens(const RawSeedEntry& other) const {
        for (int i = 0; i < n; ++i) {
            if (tokens[i] != other.tokens[i]) return false;
        }
        return true;
    }
};

const std::vector<uint32_t>& CorpusMiner::fetch_doc(uint32_t doc_id) const {

    if (in_memory_only) {
       return docs[doc_id];
    }

    std::lock_guard<std::mutex> lock(cache_mtx);

    // 1. Check Cache
    auto it = doc_cache.find(doc_id);
    if (it != doc_cache.end()) return it->second;

    // 2. Manage Cache Size (Simple FIFO/Random eviction)
    if (doc_cache.size() >= max_cache_size) {
        doc_cache.erase(doc_cache.begin());
    }

    // 3. Read from Disk
    std::vector<uint32_t> doc(doc_lengths[doc_id]);
    std::ifstream bin_in(bin_corpus_path, std::ios::binary);
    bin_in.seekg(doc_offsets[doc_id]);
    bin_in.read((char*)doc.data(), doc_lengths[doc_id] * sizeof(uint32_t));

    return doc_cache[doc_id] = std::move(doc);
}

void CorpusMiner::load_directory(const std::string& path, double sampling) {
    auto total_start = start_timer();

    std::cout << "[LOG] Scanning directory: " << path << std::endl;
    std::vector<fs::path> paths;
    for (const auto& entry : fs::recursive_directory_iterator(path)) {
        if (entry.path().extension() == ".txt") paths.push_back(entry.path());
    }

    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(paths.begin(), paths.end(), g);

    size_t total_files = paths.size();
    size_t n = static_cast<size_t>(total_files * sampling);
    if (n > total_files) n = total_files;
    paths.resize(n);

    std::cout << "[LOG] Found " << total_files << " .txt files. Processing " << n
              << " files (sampling rate: " << (sampling * 100) << "%)" << std::endl;
    std::vector<std::vector<std::string>> raw_docs(n);
    if (max_threads > 0) omp_set_num_threads(max_threads);
    std::cout << "[LOG] Phase I: Parallel tokenization..." << std::endl;
    auto p1_start = start_timer();

    #pragma omp parallel for
    for (size_t i = 0; i < n; ++i) {
        std::ifstream file(paths[i], std::ios::binary);
        if (file) {
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            raw_docs[i] = tokenize(content);
        }
    }
    stop_timer("Tokenization", p1_start);

    std::cout << "[LOG] Phase II: Building dictionary, encoding ID, and counting DF..." << std::endl;
    auto p2_start = start_timer();
    docs.clear();
    if (in_memory_only) docs.reserve(n);
    file_paths.reserve(n);

    std::vector<uint32_t> word_last_doc_id;
    word_df.clear();

    // Only open bin file if NOT in-memory mode
    std::unique_ptr<std::ofstream> bin_out;
    if (!in_memory_only) {
        bin_out = std::make_unique<std::ofstream>(bin_corpus_path, std::ios::binary);
    }

    for (size_t i = 0; i < n; ++i) {
            file_paths.push_back(paths[i].string());
            std::vector<uint32_t> encoded;
            encoded.reserve(raw_docs[i].size());

            for (const auto& w : raw_docs[i]) {
                uint32_t w_id;
                auto it = word_to_id.find(w);
                if (it == word_to_id.end()) {
                    w_id = id_to_word.size();
                    word_to_id[w] = w_id;
                    id_to_word.push_back(w);
                    word_df.push_back(0);
                    word_last_doc_id.push_back(0);
                } else {
                    w_id = it->second;
                }
                encoded.push_back(w_id);

                if (word_last_doc_id[w_id] != (uint32_t)i + 1) {
                    word_df[w_id]++;
                    word_last_doc_id[w_id] = (uint32_t)i + 1;
                }
            }

            doc_lengths.push_back(encoded.size());

            if (in_memory_only) {
                docs.push_back(std::move(encoded));
            } else {
                doc_offsets.push_back(bin_out->tellp());
                bin_out->write((char*)encoded.data(), encoded.size() * sizeof(uint32_t));

                // If preload is requested, keep in cache while building
                if (preload_cache && doc_cache.size() < max_cache_size) {
                    doc_cache[i] = encoded; // Copy before clearing
                }
                encoded.clear();
            }
            raw_docs[i].clear();
        }
    word_last_doc_id.clear();
    word_last_doc_id.shrink_to_fit();
    stop_timer("Dictionary, Encoding & DF counting", p2_start);
    stop_timer("Total Loading", total_start);
}

size_t CorpusMiner::get_current_rss_mb() {
    std::ifstream stat_stream("/proc/self/statm", std::ios_base::in);
    unsigned long long pages;
    stat_stream >> pages;
    stat_stream >> pages;
    return (pages * sysconf(_SC_PAGESIZE)) / (1024 * 1024);
}

inline uint64_t hash_tokens(const uint32_t* tokens, int n) {
    uint64_t h = 14695981039346656037ULL; // FNV offset basis
    for (int i = 0; i < n; ++i) {
        h ^= static_cast<uint64_t>(tokens[i]);
        h *= 1099511628211ULL; // FNV prime
    }
    return h;
}

void CorpusMiner::mine(int min_docs, int ngrams, const std::string& output_csv) {
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
            const std::vector<uint32_t>* doc_ptr = (in_memory_only) ? &docs[d] : &local_doc;
            if (!in_memory_only) {
                local_doc.resize(doc_lengths[d]);
                local_bin.seekg(doc_offsets[d]);
                local_bin.read((char*)local_doc.data(), doc_lengths[d] * sizeof(uint32_t));
            }

            if (doc_ptr->size() < (size_t)ngrams) continue;

            for (uint32_t p = 0; p <= doc_ptr->size() - ngrams; ++p) {
                uint64_t h = hash_tokens(doc_ptr->data() + p, ngrams);
                size_t idx = h % filter_size;

                uint8_t* target = &filter_counters[idx];
                uint8_t current = __atomic_load_n(target, __ATOMIC_RELAXED);
                while (current < 255) {
                    if (__atomic_compare_exchange_n(target, &current, current + 1, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
                        break;
                }
            }
        }
    }
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
        std::cout << "\n[LOG] Flushing " << buffer.size() << " seeds to disk... (RAM: " << get_current_rss_mb() << " MB)" << std::endl;
        std::sort(std::execution::par, buffer.begin(), buffer.end(), [](const RawSeedEntry& a, const RawSeedEntry& b) {
            for (int i = 0; i < a.n; ++i) if (a.tokens[i] != b.tokens[i]) return a.tokens[i] < b.tokens[i];
            return a.doc_id < b.doc_id;
        });
        std::string fname = temp_dir + "/chunk_" + std::to_string(chunk_id++) + ".bin";
        chunk_files.push_back(fname);
        std::ofstream out(fname, std::ios::binary);
        if (out) out.write((char*)buffer.data(), buffer.size() * sizeof(RawSeedEntry));
        buffer.clear();
        buffer.shrink_to_fit();
    };

    for (uint32_t d = 0; d < doc_lengths.size(); ++d) {
        if (memory_limit_mb > 0 && get_current_rss_mb() >= (size_t)(memory_limit_mb * 0.75)) flush_buffer();
        const auto& current_doc = fetch_doc(d);
        if (current_doc.size() < (size_t)ngrams) continue;

        for (uint32_t p = 0; p <= current_doc.size() - ngrams; ++p) {
            total_processed++;
            uint64_t h = hash_tokens(&current_doc[p], ngrams);

            // Bloom Filter check
            if (filter_counters[h % filter_size] >= (uint8_t)std::min(min_docs, 255)) {
                // DF check
                bool df_ok = true;
                for (int i = 0; i < ngrams; ++i) {
                    if (word_df[current_doc[p+i]] < (uint32_t)min_docs) { df_ok = false; break; }
                }

                if (df_ok) {
                    RawSeedEntry entry;
                    std::memset(&entry, 0, sizeof(RawSeedEntry));
                    entry.n = ngrams; entry.doc_id = d; entry.pos = p;
                    for (int i = 0; i < ngrams; ++i) entry.tokens[i] = current_doc[p + i];
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
            std::cout << "[LOG] Scanning: " << (d + 1) << "/" << doc_lengths.size() << " | Seeds Found: " << seeds_passed << " \r" << std::flush;
        }
    }

    // Print Efficiency Statistics
    double efficiency = (total_processed > 0) ? (100.0 * seeds_rejected / total_processed) : 0;
    std::cout << "\n[BLOOM STATS] Total n-grams: " << total_processed << std::endl;
    std::cout << "[BLOOM STATS] Accepted:    " << seeds_passed << std::endl;
    std::cout << "[BLOOM STATS] Rejected:    " << seeds_rejected << " (" << efficiency << "% reduction)" << std::endl;

    filter_counters.clear();
    filter_counters.shrink_to_fit();
    if (in_memory_only) {
        std::cout << "[LOG] In-Memory Mode: Sorting all " << buffer.size() << " seeds in RAM..." << std::endl;
        std::sort(std::execution::par, buffer.begin(), buffer.end(), [](const RawSeedEntry& a, const RawSeedEntry& b) {
            for (int i = 0; i < a.n; ++i) {
                if (a.tokens[i] != b.tokens[i]) return a.tokens[i] < b.tokens[i];
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
                    for(int k = 0; k < ngrams; ++k) tokens_vec[k] = representative.tokens[k];
                    candidates.push_back({tokens_vec, std::move(current_occs), unique_docs.size()});
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
                    if (!stream.read((char*)&current, sizeof(RawSeedEntry))) {
                        active = false;
                        return false;
                    }
                    return true;
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
                    for(int k = 0; k < ngrams; ++k) tokens_vec[k] = representative.tokens[k];
                    candidates.push_back({tokens_vec, std::move(current_occs), unique_docs.size()});
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
                    std::cout << "[LOG] Step 1.5: Temporary directory and chunk files removed." << std::endl;
                }
            } catch (const fs::filesystem_error& e) {
                std::cerr << "[WARNING] Cleanup failed: " << e.what() << std::endl;
            }
        }
        // --- END OF STEP 1.5 ---

    size_t total_seeds_generated = candidates.size();
    stop_timer(std::to_string(ngrams) + "-gram Seed Generation (Disk)", s1_start);

    std::cout << "[LOG] Step 2: Sorting " << candidates.size() << " candidates by score (support * length)..." << std::endl;

    std::sort(std::execution::par, candidates.begin(), candidates.end(), [](const Phrase& a, const Phrase& b) {
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
    for(size_t i = 0; i < doc_lengths.size(); ++i) {
        processed[i].assign(doc_lengths[i], false);
    }

    for (size_t c_idx = 0; c_idx < candidates.size(); ++c_idx) {
        if (g_stop_requested) {
            std::cout << "\n[!] Expansion interrupted. Moving to save results..." << std::endl;
            break;
        }

        if (c_idx % 100 == 0 || c_idx == candidates.size() - 1) {
            std::cout << "[LOG] Expanding: " << (c_idx + 1) << "/" << candidates.size()
                      << " | Phrases found: " << final_phrases.size() << "          \r" << std::flush;
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
                // Использование кэшированного чтения документа с диска
                const auto& doc = fetch_doc(o.doc_id);
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

                if (unique_docs.size() >= (size_t)min_docs && unique_docs.size() >= max_support) {
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

        for (auto& o : cand.occs) {
            for (uint32_t i = 0; i < (uint32_t)cand.tokens.size(); ++i) {
                if (o.pos + i < processed[o.doc_id].size())
                    processed[o.doc_id][o.pos + i] = true;
            }
        }
        final_phrases.push_back(std::move(cand));
    }
    std::cout << std::endl;
    stop_timer("Expansion & Pruning", s3_start);

    size_t count_6plus = 0;
    for (const auto& p : final_phrases) if (p.tokens.size() >= 6) count_6plus++;

    std::cout << "\n========== MINING STATISTICS ==========" << std::endl;
    std::cout << "Candidates after merge:       " << total_seeds_generated << std::endl;
    std::cout << "Total phrases mined:          " << final_phrases.size() << std::endl;
    std::cout << "Long phrases (6+ words):      " << count_6plus << std::endl;
    std::cout << "=======================================\n" << std::endl;

    std::cout << "[LOG] Step 4: Saving results to " << output_csv << "..." << std::endl;
    auto s4_start = start_timer();
    save_to_csv(final_phrases, output_csv);
    stop_timer("CSV Saving", s4_start);

    stop_timer("Total Mining Process", mine_start);
}

void CorpusMiner::save_to_csv(const std::vector<Phrase>& res, const std::string& out_p) {
    std::ofstream f(out_p);
    if (!f.is_open()) return;

    f << "phrase,freq,length,example_files\n";
    for (const auto& p : res) {
        f << "\"";
        for (size_t i = 0; i < p.tokens.size(); ++i) {
            // Safety check for dictionary lookup
            if (p.tokens[i] < id_to_word.size()) {
                f << id_to_word[p.tokens[i]] << (i == p.tokens.size()-1 ? "" : " ");
            }
        }
        f << "\"," << p.support << "," << p.tokens.size() << ",\"";

        std::unordered_set<uint32_t> d_ids;
        for (auto& o : p.occs) d_ids.insert(o.doc_id);

        size_t count = 0;
        for (auto id : d_ids) {
            if (id < file_paths.size()) {
                f << file_paths[id];
                if (++count >= 2) break; // Limit to 2 examples
                if (count < d_ids.size()) f << "|";
            }
        }
        f << "\"\n";
    }
}
