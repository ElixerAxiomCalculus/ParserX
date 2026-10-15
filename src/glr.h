#ifndef GLR_H
#define GLR_H

#include <ostream>
#include <string>
#include <vector>

#include "grammar.h"
#include "lexer.h"
#include "table.h"

struct GlrResult {
    bool accepted = false;
    bool aborted = false;
    std::string abortReason;
    bool infinite = false;
    unsigned long long parses = 0;
    bool saturated = false;
    int gssNodes = 0;
    int gssEdges = 0;
    int sppfNodes = 0;
    int packedAlternatives = 0;
    int errorToken = -1;
    // Forest nodes with more than one alternative: "S over tokens 0..16 (2 alternatives)"
    std::vector<std::string> ambiguities;
};

// Tomita-style generalized LR parsing on the UNRESOLVED table: every
// candidate action of a conflict cell is followed on a graph-structured
// stack, and the parses are kept in a shared packed parse forest whose
// distinct trees are counted. Work is capped to stop exponential blow-up.
GlrResult glrParse(const Grammar& g, const ParsingTable& t, const std::vector<Token>& tokens);

void printGlr(std::ostream& os, const GlrResult& r);

#endif
