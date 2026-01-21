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
#include <cstdio>

namespace fs = std::filesystem;

const int SMALL_NGRAMS_THRESHOLD = 16;  // Use fixed array for n <= this value
const int MAX_NGRAMS_FIXED = 16;       // Maximum size for fixed array
const int DEBUG = 0; // to see internal structures in the console

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
    RawSeedEntry(const RawSeedEntry& other) : doc_id(other.doc_id), pos(other.pos), n(other.n), is_dynamic(other.is_dynamic) {
        if (is_dynamic) {
            tokens.dynamic_tokens = new std::vector<uint32_t>(*other.tokens.dynamic_tokens);
        } else {
            std::memcpy(tokens.fixed_tokens, other.tokens.fixed_tokens, sizeof(tokens.fixed_tokens));
        }
    }

    // Move constructor
    RawSeedEntry(RawSeedEntry&& other) noexcept : doc_id(other.doc_id), pos(other.pos), n(other.n), is_dynamic(other.is_dynamic) {
        if (is_dynamic) {
            tokens.dynamic_tokens = other.tokens.dynamic_tokens;
            other.tokens.dynamic_tokens = nullptr;
        } else {
            std::memcpy(tokens.fixed_tokens, other.tokens.fixed_tokens, sizeof(tokens.fixed_tokens));
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
            std::memcpy(tokens.fixed_tokens, other.tokens.fixed_tokens, sizeof(tokens.fixed_tokens));
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
            std::memcpy(tokens.fixed_tokens, other.tokens.fixed_tokens, sizeof(tokens.fixed_tokens));
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
            uint32_t this_token = (is_dynamic) ? (*tokens.dynamic_tokens)[i] : tokens.fixed_tokens[i];
            uint32_t other_token = (other.is_dynamic) ? (*other.tokens.dynamic_tokens)[i] : other.tokens.fixed_tokens[i];
            if (this_token != other_token) return this_token > other_token;
        }
        if (doc_id != other.doc_id) return doc_id > other.doc_id;
        return pos > other.pos;
    }

    bool same_tokens(const RawSeedEntry& other) const {
        for (int i = 0; i < n; ++i) {
            uint32_t this_token = (is_dynamic) ? (*tokens.dynamic_tokens)[i] : tokens.fixed_tokens[i];
            uint32_t other_token = (other.is_dynamic) ? (*other.tokens.dynamic_tokens)[i] : other.tokens.fixed_tokens[i];
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

// boilerplate-buster/corpus_miner.cpp

void CorpusMiner::load_csv(const std::string& path, char delimiter, double sampling) {
    auto total_start = start_timer();
    std::cout << "[LOG] Loading CSV: " << path << " (Delimiter: '" << delimiter << "')" << std::endl;

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "[ERROR] Could not open CSV file: " << path << std::endl;
        return;
    }

    std::vector<std::string> rows;
    std::string currentRow;
    std::string currentField;
    bool inQuotes = false;
    char c;

    // Phase 0: Robust CSV Parsing
    while (file.get(c)) {
        if (inQuotes) {
            if (c == '"') {
                if (file.peek() == '"') {
                    currentField += '"';
                    file.get();
                } else {
                    inQuotes = false;
                }
            } else {
                currentField += c;
            }
        } else {
            if (c == '"') {
                inQuotes = true;
            } else if (c == delimiter) {
                if (!currentRow.empty()) currentRow += " ";
                currentRow += currentField;
                currentField.clear();
            } else if (c == '\n' || c == '\r') {
                if (!currentRow.empty() || !currentField.empty()) {
                    if (!currentRow.empty()) currentRow += " ";
                    currentRow += currentField;
                    rows.push_back(std::move(currentRow));
                    currentRow.clear();
                    currentField.clear();
                }
                if (c == '\r' && file.peek() == '\n') file.get();
            } else {
                currentField += c;
            }
        }
    }
    if (!currentRow.empty() || !currentField.empty()) {
        if (!currentRow.empty()) currentRow += " ";
        currentRow += currentField;
        rows.push_back(std::move(currentRow));
    }

    if (sampling < 1.0) {
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(rows.begin(), rows.end(), g);
        rows.resize(static_cast<size_t>(rows.size() * sampling));
    }

    size_t n = rows.size();
    std::vector<std::vector<std::string>> raw_docs(n);
    if (max_threads > 0) omp_set_num_threads(max_threads);

    #pragma omp parallel for
    for (size_t i = 0; i < n; ++i) {
        raw_docs[i] = tokenize(rows[i]);
        rows[i].clear();
    }
    rows.clear();

    // Phase II: Encoding and Binary Persistence
    docs.clear();
    if (in_memory_only) docs.reserve(n);
    file_paths.reserve(n);
    std::vector<uint32_t> word_last_doc_id;
    word_df.clear();

    // Open binary file if not in-memory mode
    std::unique_ptr<std::ofstream> bin_out;
    if (!in_memory_only) {
        bin_out = std::make_unique<std::ofstream>(bin_corpus_path, std::ios::binary);
    }

    for (size_t i = 0; i < n; ++i) {
        file_paths.push_back("row_" + std::to_string(i));
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
            // FIX: Populate doc_offsets and write to disk
            doc_offsets.push_back(bin_out->tellp());
            bin_out->write((char*)encoded.data(), encoded.size() * sizeof(uint32_t));
            encoded.clear();
        }
        raw_docs[i].clear();
    }
    stop_timer("CSV Loading & Encoding", total_start);
}

void CorpusMiner::load_directory(const std::string& path, double sampling) {
    auto total_start = start_timer();

    std::cout << "[LOG] Scanning directory: " << path << (file_mask.empty() ? " (All files)" : " (Mask: " + file_mask + ")") << std::endl;
        std::vector<fs::path> paths;

        for (const auto& entry : fs::recursive_directory_iterator(path)) {
            if (!fs::is_regular_file(entry)) continue;

            bool match = false;
            if (file_mask.empty() || file_mask == "*") {
                match = true;
            } else if (file_mask.size() >= 2 && file_mask.substr(0, 2) == "*.") {
                // Wildcard extension match (e.g., "*.txt" -> ".txt")
                std::string target_ext = file_mask.substr(1);
                if (entry.path().extension() == target_ext) match = true;
            } else {
                // Exact filename match
                if (entry.path().filename() == file_mask) match = true;
            }

            if (match) paths.push_back(entry.path());
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
        if (!file) continue;

        unsigned char bom[2] = {0, 0};
        file.read((char*)bom, 2);

        if (bom[0] == 0xFF && bom[1] == 0xFE) {
            // UTF-16 Little Endian
            std::vector<char> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            std::u16string u16_content;
            u16_content.resize(buffer.size() / 2);
            std::memcpy(u16_content.data(), buffer.data(), u16_content.size() * 2);
            raw_docs[i] = tokenize_utf16(u16_content);
        }
        else if (bom[0] == 0xFE && bom[1] == 0xFF) {
            // UTF-16 Big Endian (Manual byte swap)
            std::vector<char> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            std::u16string u16_content;
            u16_content.reserve(buffer.size() / 2);
            for (size_t j = 0; j + 1 < buffer.size(); j += 2) {
                u16_content.push_back((char16_t)(((unsigned char)buffer[j] << 8) | (unsigned char)buffer[j+1]));
            }
            raw_docs[i] = tokenize_utf16(u16_content);
        }
        else {
            // Standard UTF-8 / ASCII logic
            file.seekg(0, std::ios::beg);
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

void CorpusMiner::save_to_csv(const std::vector<Phrase>& res, const std::string& out_p) {
    std::ofstream f(out_p);
    if (!f.is_open()) return;
    std::cout << "[LOG] Saving to " << out_p << std::endl;
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

void CorpusMiner::export_to_spmf(const std::string& path) const {
    std::ofstream out(path);
    if (!out.is_open()) {
        // Рекомендуется добавить проверку открытия файла
        return;
    }

    for (uint32_t i = 0; i < num_docs(); ++i) {
        const auto& doc = get_doc(i);
        for (size_t j = 0; j < doc.size(); ++j) {
            // Записываем элемент и сразу после него -1 (конец айтемсета)
            out << doc[j] << " -1 ";
        }
        // В конце каждой строки записываем -2 (конец последовательности)
        out << "-2\n";
    }
}

void CorpusMiner::import_from_spmf(const std::string& spmf_out, const std::string& final_csv) {
    std::ifstream in(spmf_out);
    std::vector<Phrase> results;
    std::string line;

    while (std::getline(in, line)) {
        if (line.empty()) continue;

        // SPMF format: "item1 item2 item3 #SUP: count"
        size_t sup_pos = line.find("#SUP:");
        if (sup_pos == std::string::npos) continue;

        std::string items_part = line.substr(0, sup_pos);
        int support = std::stoi(line.substr(sup_pos + 5));

        std::stringstream ss(items_part);
        uint32_t token;
        std::vector<uint32_t> tokens;
        while (ss >> token) tokens.push_back(token);

        // SPMF doesn't provide positions, so we store doc_ids by re-scanning or leaving empty.
        // For compatibility with save_to_csv, we'll just store the support count.
        results.push_back({tokens, {}, (size_t)support});
    }

    std::cout << "[SPMF] Parsed " << results.size() << " phrases from SPMF output." << std::endl;
    save_to_csv(results, final_csv);
}

void CorpusMiner::run_spmf(const std::string& algo,
                           const std::string& spmf_params,
                           const std::string& jar_path,
                           int min_docs,
                           const std::string& output_csv) {
    std::string input_tmp = "spmf_input.txt";
    std::string output_tmp = "spmf_output.txt";

    std::cout << "[SPMF] Converting corpus to SPMF format..." << std::endl;
    export_to_spmf(input_tmp);

    // Construct Command
    // Format: java -jar spmf.jar run Algorithm input output params
    std::string cmd = "java -jar " + jar_path + " run " + algo + " " + input_tmp + " " + output_tmp + " " + spmf_params;

    std::cout << "[SPMF] Executing: " << cmd << std::endl;

    auto start = start_timer();
    int ret = std::system(cmd.c_str());
    stop_timer("SPMF Java Execution", start);

    if (ret != 0) {
        std::cerr << "[ERROR] SPMF execution failed with code " << ret << std::endl;
    } else {
        import_from_spmf(output_tmp, output_csv);
    }

    // Cleanup
    std::filesystem::remove(input_tmp);
    std::filesystem::remove(output_tmp);
}