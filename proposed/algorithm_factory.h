#pragma once

#include <memory>
#include <string>
#include "mining_algorithm.h"
#include "bloom_gram_miner.h"
#include "bide/bide_miner.h"

enum class AlgorithmKind {
    BloomNgram,
    Bide, // Add this
};

inline AlgorithmKind parse_algorithm_kind(const std::string& name) {
    if (name == "bloom" || name == "bloom_ngram" || name == "default")
        return AlgorithmKind::BloomNgram;
    if (name == "bide") // Add this
        return AlgorithmKind::Bide;

    throw std::runtime_error("Unknown algorithm name: " + name);
}

inline std::unique_ptr<IMiningAlgorithm> make_algorithm(AlgorithmKind kind) {
    switch (kind) {
        case AlgorithmKind::BloomNgram:
            return std::make_unique<BloomNgramMiner>();
        case AlgorithmKind::Bide: // Add this
            return std::make_unique<BideMiner>();
    }
    throw std::runtime_error("Unsupported algorithm kind");
}