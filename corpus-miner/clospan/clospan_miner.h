#pragma once

#include "../mining_algorithm.h"
#include "../corpus_miner.h"
#include "../types.h"
#include <unordered_map>
#include <vector>


class CloSpanMiner : public IMiningAlgorithm {
public:
    std::string name() const override { return "clospan"; }

    std::vector<Phrase> mine(const CorpusMiner& corpus,
                             const MiningParams& params) override;

private:
    // Pruning: Forward Closure Check
    bool is_forward_closed(int current_sup, const std::unordered_map<uint32_t, SupportInfo>& extensions);

    // Pruning: Backward Closure Check (The "Left" extension check)
    bool is_backward_closed(const CorpusMiner& corpus,
                            const std::vector<uint32_t>& patt,
                            const std::vector<Occurrence>& matches);
};