#pragma once

#include <memory>
#include <string>
#include "mining_algorithm.h"
#include "_ours/bloom_gram_miner.h"
#include "bide/bide_miner.h"
#include "clospan/clospan_miner.h"

enum class AlgorithmKind {
    BloomNgram,
    Bide,
    CloSpan,
};

inline AlgorithmKind parse_algorithm_kind(const std::string& name) {
    if (name == "bloomspan" || name == "default")
        return AlgorithmKind::BloomNgram;
    if (name == "bide")
        return AlgorithmKind::Bide;
    if (name == "clospan")
        return AlgorithmKind::CloSpan;

    throw std::runtime_error("Unknown algorithm name: " + name);
}

inline std::unique_ptr<IMiningAlgorithm> make_algorithm(AlgorithmKind kind) {
    switch (kind) {
        case AlgorithmKind::BloomNgram:
            return std::make_unique<BloomNgramMiner>();
        case AlgorithmKind::Bide:
            return std::make_unique<BideMiner>();
        case AlgorithmKind::CloSpan: // Add this
            return std::make_unique<CloSpanMiner>();
    }
    throw std::runtime_error("Unsupported algorithm kind");
}