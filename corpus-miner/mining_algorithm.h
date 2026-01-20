#pragma once

#include <string>
#include <vector>
#include "types.h"

class CorpusMiner;  // forward declaration

// Generic params for mining (extend as needed later)
struct MiningParams {
    int min_docs;
    int ngrams;
    std::string output_csv;
};

// Abstract interface for all sequence mining algorithms
class IMiningAlgorithm {
public:
    virtual ~IMiningAlgorithm() = default;

    // Human-readable name (for logs)
    virtual std::string name() const = 0;

    // Core mining operation.
    // Reads everything it needs from the CorpusMiner (corpus, DF, offsets, etc.)
    // and returns the mined phrases.
    virtual std::vector<Phrase> mine(const CorpusMiner& corpus,
                                     const MiningParams& params) = 0;
};