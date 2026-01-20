#pragma once

#include "../mining_algorithm.h"
#include "../corpus_miner.h"

// Your existing n-gram + Bloom + expansion miner, refactored into a class
class BloomNgramMiner : public IMiningAlgorithm {
public:
    std::string name() const override { return "bloom_ngram"; }

    std::vector<Phrase> mine(const CorpusMiner& corpus,
                             const MiningParams& params) override;
};
