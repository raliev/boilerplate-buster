#pragma once

#include "../mining_algorithm.h"
#include "../corpus_miner.h"
#include <unordered_map>
#include <vector>
#include <functional>

// Optimization: Bundling count and matches reduces hash map lookups
struct SupportInfo {
    int count = 0;
    std::vector<Occurrence> matches;
};

class BideMiner : public IMiningAlgorithm {
public:
    std::string name() const override { return "bide"; }

    std::vector<Phrase> mine(const CorpusMiner& corpus,
                             const MiningParams& params) override;

private:
    // Signatures updated to use SupportInfo to match the .cpp implementation
    bool is_forward_closed(int current_sup, const std::unordered_map<uint32_t, SupportInfo>& extensions);

    bool is_backward_closed(const CorpusMiner& corpus,
                            const std::vector<uint32_t>& patt,
                            const std::vector<Occurrence>& matches);
};