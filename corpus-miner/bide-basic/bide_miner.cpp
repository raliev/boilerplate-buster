#include "bide_miner.h"
#include "../signal_handler.h"
#include <functional>

// Forward Closure Check 
bool BideMiner::is_forward_closed(int current_sup, const std::unordered_map<uint32_t, std::vector<Occurrence>>& extensions) {
    for (const auto& entry : extensions) {
        if ((int)entry.second.size() == current_sup) return false;
    }
    return true;
}

// BIDE Backward Extension Check 
bool BideMiner::is_backward_closed(const CorpusMiner& corpus, 
                                   const std::vector<uint32_t>& patt, 
                                   const std::vector<Occurrence>& matches) {
    if (patt.empty() || matches.empty()) return true;

    int common_prev = -2; // -2: uninitialized, -1: mismatch

    for (const auto& m : matches) {
        uint32_t doc_id = m.doc_id;
        uint32_t end_pos = m.pos;
        const auto& doc = corpus.get_doc(doc_id);

        int prev_pos = (int)end_pos - (int)patt.size();
        if (prev_pos < 0) return true;

        int item = (int)doc[prev_pos];
        if (common_prev == -2) common_prev = item;
        else if (common_prev != item) return true; 
    }
    return common_prev == -1;
}

std::vector<Phrase> BideMiner::mine(const CorpusMiner& corpus, const MiningParams& params) {
    std::vector<Phrase> results;
    int min_sup = params.min_docs;

    // Helper to find next entries (projected DB logic) 
    auto get_next_entries = [&](const std::vector<Occurrence>& matches) {
        std::unordered_map<uint32_t, std::vector<Occurrence>> occurs;
        for (const auto& m : matches) {
            const auto& seq = corpus.get_doc(m.doc_id);
            std::unordered_map<uint32_t, uint32_t> first_seen;
            for (uint32_t j = m.pos + 1; j < (uint32_t)seq.size(); ++j) {
                uint32_t item = seq[j];
                if (first_seen.find(item) == first_seen.end()) {
                    first_seen[item] = j;
                    occurs[item].push_back({m.doc_id, j});
                }
            }
        }
        return occurs;
    };

    std::function<void(std::vector<uint32_t>&, const std::vector<Occurrence>&)> bide_rec;
    bide_rec = [&](std::vector<uint32_t>& patt, const std::vector<Occurrence>& matches) {
        if (g_stop_requested) return;

        auto occurs = get_next_entries(matches);
        int current_sup = (int)matches.size();

        // BIDE Pruning: Check backward closure 
        if (!is_backward_closed(corpus, patt, matches)) return;

        // Check forward closure for result validity 
        bool is_closed = is_forward_closed(current_sup, occurs);

        if (patt.size() >= 1 && is_closed) {
            results.push_back({patt, matches, (size_t)current_sup});
        }

        // Recursive extension 
        for (auto& [item, new_matches] : occurs) {
            if ((int)new_matches.size() >= min_sup) {
                patt.push_back(item);
                bide_rec(patt, new_matches);
                patt.pop_back();
            }
        }
    };

    // Initial projection 
    std::vector<Occurrence> initial;
    for (uint32_t i = 0; i < (uint32_t)corpus.num_docs(); ++i) {
        initial.push_back({i, (uint32_t)-1});
    }

    std::vector<uint32_t> current_patt;
    bide_rec(current_patt, initial);

    return results;
}