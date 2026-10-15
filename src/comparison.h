#ifndef COMPARISON_H
#define COMPARISON_H

#include <chrono>
#include <ostream>
#include <string>
#include <vector>

#include "compress.h"
#include "first_follow.h"
#include "lalr_dp.h"
#include "lexer.h"
#include "parser.h"
#include "table.h"

struct ParserReport {
    std::string name;
    int states = 0;
    int actionEntries = 0;
    int gotoEntries = 0;
    int srConflicts = 0;
    int rrConflicts = 0;
    bool accepted = false;
    int steps = 0;
    long buildMicroseconds = 0;
    long parseMicroseconds = 0;
    int errorStep = -1;
    int reductionsBeforeError = 0;
    int errorLine = 0;
    int errorColumn = 0;
    std::string errorToken;
    std::vector<std::string> expected;
    int errorTokenIndex = -1;
    int conflictMoves = 0;
    std::string context;
    bool recoverRan = false;
    int recoverErrors = 0;
    int recoverSkipped = 0;
    int recoverFirstLine = 0;
    int recoverFirstColumn = 0;
    std::string recoverAbort;
};

struct CompareOptions {
    bool recover = false;
    int maxErrors = 50;
    bool compressed = false;
};

struct ComparisonResult {
    std::vector<ParserReport> parsers;
};

// lalr / lalrMachine hold the table chosen by --lalr-method; both LALR
// constructions are always built so they can be timed and verified.
struct BuiltTables {
    LRMachine slrMachine, clrMachine, lalrMachine;
    LRMachine lalrMergeMachine, lalrDpMachine, pgmMachine;
    ParsingTable slr, clr, lalr;
    ParsingTable lalrMerge, lalrDp, pgm;
    std::string lalrMethod = "merge";
    DPStats dpStats;
    long slrMicroseconds = 0;
    long clrMicroseconds = 0;
    long lalrMicroseconds = 0;
    long lalrMergeMicroseconds = 0;
    long lalrDpMicroseconds = 0;
    long pgmMicroseconds = 0;
    PackedTable slrPacked, lalrPacked, clrPacked, pgmPacked;
};

const PackedTable& packedFor(const BuiltTables& b, const std::string& name);

BuiltTables buildAllTables(const Grammar& g, const FirstFollow& ff,
                           const std::string& lalrMethod = "merge");

ComparisonResult compareParsers(const Grammar& g,
                                const std::vector<Token>& tokens,
                                const BuiltTables& tables,
                                const CompareOptions& opt = CompareOptions());

void printComparison(std::ostream& os, const ComparisonResult& c);

#endif
