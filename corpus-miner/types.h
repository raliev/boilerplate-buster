#ifndef TYPES_H
#define TYPES_H

#include <vector>
#include <cstdint>

struct Occurrence {
    uint32_t doc_id;
    uint32_t pos;
};

struct Phrase {
    std::vector<uint32_t> tokens;
    std::vector<Occurrence> occs;
    size_t support;
};

struct SupportInfo {
    int count = 0;
    std::vector<Occurrence> matches;
};

struct VectorHasher {
    size_t operator()(const std::vector<uint32_t>& v) const {
        size_t seed = v.size();
        for (auto x : v) seed ^= x + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

#endif // TYPES_H
