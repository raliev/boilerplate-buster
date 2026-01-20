#pragma once

#include "../mining_algorithm.h"
#include "../corpus_miner.h"
#include <unordered_map>

class BideMiner : public IMiningAlgorithm {
public:
    std::string name() const override { return "bide"; }

    std::vector<Phrase> mine(const CorpusMiner& corpus,
                             const MiningParams& params) override;

private:
    // Ported from bide.txt: closed.hpp
    bool is_forward_closed(int current_sup, const std::unordered_map<uint32_t, std::vector<Occurrence>>& extensions);
    bool is_backward_closed(const CorpusMiner& corpus,
                            const std::vector<uint32_t>& patt,
                            const std::vector<Occurrence>& matches);
};