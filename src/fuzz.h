#ifndef FUZZ_H
#define FUZZ_H

#include <ostream>
#include <random>
#include <string>
#include <vector>

#include "comparison.h"
#include "grammar.h"
#include "lexer.h"

struct FuzzOptions {
    int count = 0;
    unsigned seed = 20261015u;
    int maxDepth = 12;
    std::string csvPath;
    bool recover = false;
    bool compressed = false;
};

// Random sentences from a grammar. Below maxDepth an alternative is picked
// at random; deeper (or past the length cap) the alternative with the
// smallest derivation height is used, which always terminates.
class SentenceGenerator {
public:
    SentenceGenerator(const Grammar& g, unsigned seed, int maxDepth);
    bool canGenerate() const;
    std::vector<std::string> sentence();
    std::vector<Token> toTokens(const std::vector<std::string>& terminals);
    int minHeight(const std::string& symbol) const;
    int minLength(const std::string& symbol) const;
    unsigned next(unsigned bound);

private:
    int pickProduction(const std::string& lhs, bool shortest);

    const Grammar& g_;
    std::mt19937 rng_;
    int maxDepth_;
    std::vector<int> height_;
    std::vector<int> length_;
};

// Differential fuzzing of SLR, LALR, CLR and PGM; returns false when an
// invariant is violated.
bool runFuzz(const Grammar& g, const BuiltTables& bt, const FuzzOptions& opt, std::ostream& os);

#endif
