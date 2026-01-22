#include "clospan_miner.h"
#include "../signal_handler.h"
#include <functional>
#include <unordered_map>
#include <algorithm>
#include "../timer.h"

bool CloSpanMiner::is_forward_closed(int current_sup, const std::unordered_map<uint32_t, SupportInfo>& extensions) {
    for (const auto& entry : extensions) {
        if (entry.second.count == current_sup) return false;
    }
    return true;
}

bool CloSpanMiner::is_backward_closed(const CorpusMiner& corpus,
                                   const std::vector<uint32_t>& patt,
                                   const std::vector<Occurrence>& matches) {
    if (patt.empty() || matches.empty()) return true;

    int current_sup = (int)matches.size();
    uint32_t pattern_len = (uint32_t)patt.size();
    std::unordered_map<uint32_t, int> back_counts;

    for (const auto& m : matches) {
        const auto& doc = corpus.get_doc(m.doc_id);
        // Phrase logic: preceding item is at index (m.pos - pattern_len)
        if (m.pos >= pattern_len) {
            uint32_t prev_item = doc[m.pos - pattern_len];
            if (++back_counts[prev_item] == current_sup) {
                return false; // Found a common backward extension
            }
        } else {
            // If even one occurrence is at the start of a document, no common prefix can exist
            return true;
        }
    }
    return true;
}

std::vector<Phrase> CloSpanMiner::mine(const CorpusMiner& corpus, const MiningParams& params) {
    std::vector<Phrase> results;
    int min_sup = params.min_docs;
    auto mine_start = start_timer();

    std::function<void(std::vector<uint32_t>&, const std::vector<Occurrence>&)> clo_rec;
    clo_rec = [&](std::vector<uint32_t>& patt, const std::vector<Occurrence>& matches) {
        if (g_stop_requested) return;

        int current_sup = (int)matches.size();

        // 1. Backward Sub-pattern Pruning
        if (!is_backward_closed(corpus, patt, matches)) return;

        // 2. Generate Extensions (Contiguous phrases)
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

       if (patt.size() >= (size_t)params.min_l && is_closed) {
           results.push_back({patt, matches, (size_t)current_sup});
       }

        // 4. Recursive Expansion (DFS)
        for (auto& [item, info] : extensions) {
            if (info.count >= min_sup) {
                patt.push_back(item);
                clo_rec(patt, info.matches);
                patt.pop_back();
            }
        }
    };

    // Initial Database Scan
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
            clo_rec(current_patt, info.matches);
        }
    }

    stop_timer("CloSpan (Closed Phrase) Mining", mine_start);
    return results;
}