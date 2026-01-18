#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <vector>
#include <string>
#include <cctype>

inline std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string current;
    current.reserve(32);

    for (size_t i = 0; i < text.length(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);

        // Logic:
        // 1. Any byte > 127 is part of a UTF-8 multi-byte sequence.
        // 2. Any ASCII byte that is alphanumeric is a word character.
        if (c > 127 || std::isalnum(c)) {
            // Only apply tolower to ASCII A-Z to avoid corrupting UTF-8 bytes
            if (c >= 'A' && c <= 'Z') {
                current += static_cast<char>(c + ('a' - 'A'));
            } else {
                current += static_cast<char>(c);
            }
        }
        // Everything else (punctuation, whitespace) is a delimiter
        else {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        }
    }

    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

#endif // TOKENIZER_H
