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
    docs.reserve(n);
    file_paths.reserve(n);

    std::vector<uint32_t> word_last_doc_id;
    word_df.clear();

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
        docs.push_back(std::move(encoded));
        raw_docs[i].clear();
    }
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

void CorpusMiner::mine(int min_docs, int ngrams, const std::string& output_csv) {
    if (max_threads > 0) {
        omp_set_num_threads(max_threads);
        std::cout << "[LOG] Threads limited to: " << max_threads << std::endl;
    }

    auto mine_start = start_timer();
    std::cout << "[LOG] Step 1: Gathering " << ngrams << "-gram seeds (Disk-based)..." << std::endl;
    auto s1_start = start_timer();

    // Настройки дискового буфера
    std::string temp_dir = "./miner_tmp";
    fs::create_directories(temp_dir);
    std::vector<std::string> chunk_files;

    std::vector<RawSeedEntry> buffer;
    // Лимит буфера: либо 500МБ, либо ваш лимит памяти минус оверхед
    size_t max_buffer_bytes = (memory_limit_mb > 0) ? (memory_limit_mb * 1024 * 1024 * 0.7) : (512 * 1024 * 1024);
    size_t max_buffer_entries = max_buffer_bytes / sizeof(RawSeedEntry);
    buffer.reserve(max_buffer_entries);

    int chunk_id = 0;
    auto flush_buffer = [&]() {
        if (buffer.empty()) return;
        std::sort(buffer.begin(), buffer.end(), [](const RawSeedEntry& a, const RawSeedEntry& b) {
            for (int i = 0; i < a.n; ++i) {
                if (a.tokens[i] != b.tokens[i]) return a.tokens[i] < b.tokens[i];
            }
            if (a.doc_id != b.doc_id) return a.doc_id < b.doc_id;
            return a.pos < b.pos;
        });
        std::string fname = temp_dir + "/chunk_" + std::to_string(chunk_id++) + ".bin";
        std::ofstream out(fname, std::ios::binary);
        out.write((char*)buffer.data(), buffer.size() * sizeof(RawSeedEntry));
        chunk_files.push_back(fname);
        buffer.clear();
    };

    // --- СБОР ДАННЫХ В ЧАНКИ ---
    for (uint32_t d = 0; d < docs.size(); ++d) {
        if (d % 500 == 0 || d == docs.size() - 1) {
            std::cout << "[LOG] Scanning: " << (d + 1) << "/" << docs.size()
                      << " | Chunks: " << chunk_id << " | RAM: " << get_current_rss_mb() << " MB\r" << std::flush;
        }

        if (g_stop_requested) break;
        if (docs[d].size() < (size_t)ngrams) continue;

        for (uint32_t p = 0; p <= docs[d].size() - ngrams; ++p) {
            bool potentially_frequent = true;
            for (int i = 0; i < ngrams; ++i) {
                if (word_df[docs[d][p + i]] < (uint32_t)min_docs) {
                    potentially_frequent = false;
                    break;
                }
            }
            if (!potentially_frequent) continue;

            RawSeedEntry entry;
            entry.n = ngrams;
            entry.doc_id = d;
            entry.pos = p;
            for (int i = 0; i < ngrams; ++i) entry.tokens[i] = docs[d][p + i];

            buffer.push_back(entry);
            if (buffer.size() >= max_buffer_entries) flush_buffer();
        }
    }
    flush_buffer();
    std::cout << std::endl;

    // --- СЛИЯНИЕ ЧАНКОВ (K-WAY MERGE) ---
    std::cout << "[LOG] Step 1.5: Merging chunks and filtering candidates..." << std::endl;
    std::vector<Phrase> candidates;

    struct ChunkReader {
        std::ifstream stream;
        RawSeedEntry current;
        bool active;
        bool next() {
            if (!stream.read((char*)&current, sizeof(RawSeedEntry))) { active = false; return false; }
            return true;
        }
    };

    auto cmp = [](ChunkReader* a, ChunkReader* b) { return a->current > b->current; };
    std::priority_queue<ChunkReader*, std::vector<ChunkReader*>, decltype(cmp)> pq(cmp);

    std::vector<std::unique_ptr<ChunkReader>> readers;
    for (const auto& file : chunk_files) {
        auto r = std::make_unique<ChunkReader>();
        r->stream.open(file, std::ios::binary);
        if (r->next()) { r->active = true; pq.push(r.get()); }
        readers.push_back(std::move(r));
    }

    while (!pq.empty()) {
        std::vector<Occurrence> current_occs;
        RawSeedEntry representative = pq.top()->current;

        // Собираем все идентичные N-граммы
        while (!pq.empty() && pq.top()->current.same_tokens(representative)) {
            ChunkReader* r = pq.top();
            pq.pop();
            current_occs.push_back({r->current.doc_id, r->current.pos});
            if (r->next()) pq.push(r);
        }

        // Проверка частотности (unique docs)
        std::unordered_set<uint32_t> unique_docs;
        for (auto& o : current_occs) unique_docs.insert(o.doc_id);

        if (unique_docs.size() >= (size_t)min_docs) {
            std::vector<uint32_t> tokens_vec(ngrams);
            for(int i=0; i<ngrams; ++i) tokens_vec[i] = representative.tokens[i];
            candidates.push_back({tokens_vec, std::move(current_occs), unique_docs.size()});
        }
    }

    // Очистка временных файлов
    readers.clear();
    for (const auto& f : chunk_files) fs::remove(f);
    fs::remove(temp_dir);

    size_t total_seeds_generated = candidates.size(); // Теперь это только выжившие семена
    stop_timer(std::to_string(ngrams) + "-gram Seed Generation (Disk)", s1_start);

    // --- ШАГ 2: СОРТИРОВКА (как было) ---
    std::cout << "[LOG] Step 2: Sorting " << candidates.size() << " candidates by support..." << std::endl;
    std::sort(std::execution::par, candidates.begin(), candidates.end(), [](const Phrase& a, const Phrase& b) {
        return a.support > b.support;
    });

    // --- ШАГ 3: РАСШИРЕНИЕ (как было) ---
    std::cout << "[LOG] Step 3: Expanding with Path Compression (Jumps)..." << std::endl;
    auto s3_start = start_timer();
    std::vector<Phrase> final_phrases;

    std::vector<std::vector<uint8_t>> processed(docs.size());
    for(size_t i=0; i<docs.size(); ++i) processed[i].assign(docs[i].size(), 0);

    for (size_t c_idx = 0; c_idx < candidates.size(); ++c_idx) {
        if (g_stop_requested) break;

        if (c_idx % 1000 == 0 || c_idx == candidates.size() - 1) {
            std::cout << "[LOG] Expansion Progress: " << (c_idx + 1) << "/" << candidates.size()
                      << " candidates | Mined: " << final_phrases.size() << " | RAM: " << get_current_rss_mb() << " MB\r" << std::flush;
        }

        auto& cand = candidates[c_idx];
        bool skip = true;
        for (auto& o : cand.occs) {
            if (processed[o.doc_id][o.pos] == 0) { skip = false; break; }
        }
        if (skip) continue;

        while (true) {
            std::unordered_map<uint32_t, std::vector<Occurrence>> next_word_occs;
            for (auto& o : cand.occs) {
                uint32_t np = o.pos + cand.tokens.size();
                if (np < docs[o.doc_id].size()) {
                    next_word_occs[docs[o.doc_id][np]].push_back(o);
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
                    processed[o.doc_id][o.pos + i] = 1;
            }
        }
        final_phrases.push_back(std::move(cand));
    }
    std::cout << std::endl;
    stop_timer("Expansion & Pruning", s3_start);

    // СТАТИСТИКА И СОХРАНЕНИЕ
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
    f << "phrase,freq,length,example_files\n";
    for (const auto& p : res) {
        f << "\"";
        for (size_t i = 0; i < p.tokens.size(); ++i) {
            f << id_to_word[p.tokens[i]] << (i == p.tokens.size()-1 ? "" : " ");
        }
        f << "\"," << p.support << "," << p.tokens.size() << ",\"";

        std::unordered_set<uint32_t> d_ids;
        for (auto& o : p.occs) d_ids.insert(o.doc_id);
        size_t count = 0;
        for (auto id : d_ids) {
            f << file_paths[id] << (count++ < 1 ? "|" : "");
            if (count > 1) break;
        }
        f << "\"\n";
    }
}
