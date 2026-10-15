#ifndef CLASSIFY_H
#define CLASSIFY_H

#include <ostream>
#include <string>

#include "comparison.h"
#include "first_follow.h"
#include "grammar.h"

// Strongest class of a grammar, judged on raw conflicts (before precedence
// or default resolution): LR(0) (reduce on every terminal), SLR(1),
// LALR(1), LR(1), or not LR(1).
struct GrammarClass {
    int lr0 = 0;
    int slr = 0;
    int lalr = 0;
    int clr = 0;
    int pgm = 0;
    std::string name;
    std::string line;
};

GrammarClass classifyGrammar(const Grammar& g, const FirstFollow& ff, const BuiltTables& bt);
void printClassification(std::ostream& os, const GrammarClass& c);

#endif
