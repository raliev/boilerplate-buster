#include "bide_miner.h"
#include "../signal_handler.h"
#include <functional>
#include <unordered_map>
#include <algorithm>
#include "../timer.h"

// BIDE+ Forward Closure Check
// A pattern is not closed if an extension has the same support
bool BideMiner::is_forward_closed(int current_sup, const std::unordered_map<uint32_t, SupportInfo>& extensions) {
    for (const auto& entry : extensions) {
        if (entry.second.count == current_sup) return false;
    }
    return true;
}

// BIDE+ Backward Extension Check
// Checks if a common item always precedes this pattern across all occurrences
bool BideMiner::is_backward_closed(const CorpusMiner& corpus,
                                   const std::vector<uint32_t>& patt,
                                   const std::vector<Occurrence>& matches) {
    if (patt.empty() || matches.empty()) return true;

    int current_sup = (int)matches.size();
    uint32_t pattern_len = (uint32_t)patt.size();

    // Map to count occurrences of items immediately preceding the pattern
    std::unordered_map<uint32_t, int> back_counts;

    for (const auto& m : matches) {
        const auto& doc = corpus.get_doc(m.doc_id);
        // Contiguous phrase check: preceding item is at index (m.pos - pattern_len)
        if (m.pos >= pattern_len) {
            uint32_t prev_item = doc[m.pos - pattern_len];
            if (++back_counts[prev_item] == current_sup) {
                return false; // Found a common backward extension
            }
        }
    }
    return true;
}

std::vector<Phrase> BideMiner::mine(const CorpusMiner& corpus, const MiningParams& params) {
    std::vector<Phrase> results;
    int min_sup = params.min_docs;

    auto mine_start = start_timer();

    // Recursive BIDE+ function using std::function for lambda recursion
    std::function<void(std::vector<uint32_t>&, const std::vector<Occurrence>&)> bide_rec;

    bide_rec = [&](std::vector<uint32_t>& patt, const std::vector<Occurrence>& matches) {
        if (g_stop_requested) return;

        int current_sup = (int)matches.size();

        // 1. BIDE+ Pruning: Backward Extension Check
        if (!is_backward_closed(corpus, patt, matches)) return;

        // 2. Generate Extensions (Pseudo-projection logic)
        // Instead of tail-scanning, we look only at the immediate next token for phrases
        std::unordered_map<uint32_t, SupportInfo> extensions;
        for (const auto& m : matches) {
            const auto& seq = corpus.get_doc(m.doc_id);
            uint32_t next_pos = m.pos + 1;

            if (next_pos < (uint32_t)seq.size()) {
                uint32_t next_item = seq[next_pos];
                auto& info = extensions[next_item];
                info.count++;
                info.matches.push_back({m.doc_id, next_pos});
            }
        }

        // 3. Forward Extension Check for Closure
        bool is_closed = is_forward_closed(current_sup, extensions);

        // Pattern length check (reverted to original >= 1 to match your MiningParams)
        if (patt.size() >= 1 && is_closed) {
            results.push_back({patt, matches, (size_t)current_sup});
        }

        // 4. Recursive Expansion
        for (auto& [item, info] : extensions) {
            if (info.count >= min_sup) {
                patt.push_back(item);
                bide_rec(patt, info.matches);
                patt.pop_back();
            }
        }
    };

    // Initial Database Projection (Scan for frequent single items)
    std::unordered_map<uint32_t, SupportInfo> root_extensions;
    for (uint32_t i = 0; i < (uint32_t)corpus.num_docs(); ++i) {
        const auto& doc = corpus.get_doc(i);
        for (uint32_t pos = 0; pos < (uint32_t)doc.size(); ++pos) {
            uint32_t item = doc[pos];
            auto& info = root_extensions[item];
            info.count++;
            info.matches.push_back({i, pos});
        }
    }

    for (auto& [item, info] : root_extensions) {
        if (info.count >= min_sup) {
            std::vector<uint32_t> current_patt = {item};
            bide_rec(current_patt, info.matches);
        }
    }

    std::cout << "\n========== MINING STATISTICS ==========" << std::endl;    
    std::cout << "Total closed patterns found:  " << results.size() << std::endl;
    std::cout << "=======================================\n" << std::endl;

    stop_timer("Total Mining Process", mine_start);

    return results;
}