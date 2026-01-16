#include "corpus_miner.h"
#include "tokenizer.h"
#include "timer.h"
#include "signal_handler.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <thread>
#include <mutex>
#include <random>

namespace fs = std::filesystem;

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

void CorpusMiner::mine(int min_docs, int ngrams, const std::string& output_csv) {
    auto mine_start = start_timer();

    std::cout << "[LOG] Step 1: Gathering " << ngrams << "-gram seeds..." << std::endl;
    auto s1_start = start_timer();
    std::unordered_map<std::vector<uint32_t>, std::vector<Occurrence>, VectorHasher> seeds;

    for (uint32_t d = 0; d < docs.size(); ++d) {
        // PROGRESS: Step 1
        if (d % 500 == 0 || d == docs.size() - 1) {
            std::cout << "[LOG] Seed Generation: " << (d + 1) << "/" << docs.size() << " documents processed...\r" << std::flush;
        }

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

            std::vector<uint32_t> ngram;
            ngram.reserve(ngrams);
            for (int i = 0; i < ngrams; ++i) {
                ngram.push_back(docs[d][p + i]);
            }
            seeds[ngram].push_back({d, p});
        }
    }
    std::cout << std::endl;

    size_t total_seeds_generated = seeds.size();

    std::vector<Phrase> candidates;
    for (auto& [toks, occs] : seeds) {
        std::unordered_set<uint32_t> unique_docs;
        for (auto& o : occs) unique_docs.insert(o.doc_id);
        if (unique_docs.size() >= (size_t)min_docs) {
            candidates.push_back({toks, std::move(occs), unique_docs.size()});
        }
    }
    seeds.clear();
    stop_timer(std::to_string(ngrams) + "-gram Seed Generation", s1_start);

    std::cout << "[LOG] Step 2: Sorting " << candidates.size() << " candidates by support..." << std::endl;
    std::sort(candidates.begin(), candidates.end(), [](const Phrase& a, const Phrase& b) {
        return a.support > b.support;
    });

    std::cout << "[LOG] Step 3: Expanding with Path Compression (Jumps)..." << std::endl;
    auto s3_start = start_timer();
    std::vector<Phrase> final_phrases;

    std::vector<std::vector<uint8_t>> processed(docs.size());
    for(size_t i=0; i<docs.size(); ++i) processed[i].assign(docs[i].size(), 0);

    for (size_t c_idx = 0; c_idx < candidates.size(); ++c_idx) {
        if (g_stop_requested) break;

        // PROGRESS: Step 3
        if (c_idx % 1000 == 0 || c_idx == candidates.size() - 1) {
            std::cout << "[LOG] Expansion Progress: " << (c_idx + 1) << "/" << candidates.size()
                      << " candidates | Mined: " << final_phrases.size() << "\r" << std::flush;
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
            for (uint32_t i = 0; i < cand.tokens.size(); ++i) {
                if (o.pos + i < processed[o.doc_id].size())
                    processed[o.doc_id][o.pos + i] = 1;
            }
        }
        final_phrases.push_back(std::move(cand));
    }
    std::cout << std::endl;
    stop_timer("Expansion & Pruning", s3_start);

    // STATISTICS
    size_t count_6plus = 0;
    for (const auto& p : final_phrases) {
        if (p.tokens.size() >= 6) count_6plus++;
    }

    std::cout << "\n========== MINING STATISTICS ==========" << std::endl;
    std::cout << "Total unique seeds generated: " << total_seeds_generated << std::endl;
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
