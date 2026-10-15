#ifndef PRECISION_H
#define PRECISION_H

#include <ostream>
#include <string>
#include <vector>

#include "comparison.h"
#include "first_follow.h"
#include "grammar.h"

// How much less precise each table is than canonical LR(1). Every CLR state
// is mapped to its SLR / LALR state (same LR(0) core) and to its PGM state
// (walking both automata with the same symbols).
struct PrecisionReport {
    long spuriousSlr = 0;
    long spuriousLalr = 0;
    long spuriousPgm = 0;
    long spuriousReduceSlrVsLalr = 0;
    double inflationSlr = 0;
    double inflationLalr = 0;
    double inflationPgm = 0;
};

PrecisionReport computePrecision(const Grammar& g, const FirstFollow& ff, const BuiltTables& bt);
void printPrecision(std::ostream& os, const PrecisionReport& p);
std::string precisionLine(const PrecisionReport& p);

#endif
