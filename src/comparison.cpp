#include "comparison.h"
#include "clr.h"
#include "lalr.h"
#include "pgm.h"
#include "slr.h"
#include "context.h"
#include "util.h"

#include <iomanip>

using namespace std;

typedef chrono::steady_clock Clock;

static long microsSince(Clock::time_point start) {
    return (long)chrono::duration_cast<chrono::microseconds>(
        Clock::now() - start).count();
}

// Timed build
BuiltTables buildAllTables(const Grammar& g, const FirstFollow& ff,
                           const string& lalrMethod) {
    BuiltTables b;
    b.lalrMethod = lalrMethod;
    Clock::time_point t0 = Clock::now();
    b.slrMachine = buildSLRMachine(g);
    long lr0Us = microsSince(t0);
    Clock::time_point t1 = Clock::now();
    b.slr = buildParsingTable(g, ff, b.slrMachine);
    b.slrMicroseconds = lr0Us + microsSince(t1);

    t0 = Clock::now();
    b.clrMachine = buildCLRMachine(g, ff);
    long clrMachineUs = microsSince(t0);
    t1 = Clock::now();
    b.clr = buildParsingTable(g, ff, b.clrMachine);
    b.clrMicroseconds = clrMachineUs + microsSince(t1);

    // LALR by merging CLR states: its time includes building the CLR automaton
    t0 = Clock::now();
    b.lalrMergeMachine = buildLALRMachine(g, b.clrMachine);
    b.lalrMerge = buildParsingTable(g, ff, b.lalrMergeMachine);
    b.lalrMergeMicroseconds = clrMachineUs + microsSince(t0);

    // LALR by DeRemer-Pennello: its time includes building the LR(0) automaton
    t0 = Clock::now();
    b.lalrDpMachine = buildLALRMachineDP(g, ff, b.slrMachine, &b.dpStats);
    b.lalrDp = buildParsingTable(g, ff, b.lalrDpMachine);
    b.lalrDpMicroseconds = lr0Us + microsSince(t0);

    bool dp = lalrMethod == "dp";
    b.lalrMachine = dp ? b.lalrDpMachine : b.lalrMergeMachine;
    b.lalr = dp ? b.lalrDp : b.lalrMerge;
    b.lalrMicroseconds = dp ? b.lalrDpMicroseconds : b.lalrMergeMicroseconds;

    t0 = Clock::now();
    b.pgmMachine = buildPGMMachine(g, ff);
    b.pgm = buildParsingTable(g, ff, b.pgmMachine);
    b.pgmMicroseconds = microsSince(t0);

    b.slrPacked.build(g, b.slr);
    b.lalrPacked.build(g, b.lalr);
    b.clrPacked.build(g, b.clr);
    b.pgmPacked.build(g, b.pgm);
    return b;
}

// Run parsers
static ParserReport reportFor(const Grammar& g, const ParsingTable& t, const PackedTable& packed,
                              long buildUs, const vector<Token>& tokens,
                              const CompareOptions& opt) {
    ParserReport r;
    r.name = t.name;
    r.states = (int)t.stateItems.size();
    r.actionEntries = t.actionEntries();
    r.gotoEntries = t.gotoEntries();
    r.srConflicts = t.srConflicts();
    r.rrConflicts = t.rrConflicts();
    r.buildMicroseconds = buildUs;
    Parser p(g, t, opt.compressed ? &packed : 0);
    Clock::time_point t0 = Clock::now();
    ParseResult pr = p.parse(tokens, false);
    r.parseMicroseconds = microsSince(t0);
    r.accepted = pr.accepted;
    r.steps = pr.stepCount;
    r.conflictMoves = pr.conflictMoves;
    if (!pr.accepted && pr.error.hasError) {
        r.errorStep = pr.errorStep;
        r.reductionsBeforeError = pr.reductionsBeforeError;
        r.errorLine = pr.error.line;
        r.errorColumn = pr.error.column;
        r.errorToken = pr.error.unexpectedType;
        r.errorTokenIndex = pr.error.tokenIndex;
        r.expected = pr.error.expected;
        r.context = describeErrorContext(g, t, pr.error);
    }
    if (opt.recover) {
        ParseOptions ro;
        ro.recordTrace = false;
        ro.recover = true;
        ro.maxErrors = opt.maxErrors;
        ParseResult rr = p.parse(tokens, ro);
        r.recoverRan = true;
        r.recoverErrors = (int)rr.errors.size();
        r.recoverSkipped = rr.skippedTokens;
        r.recoverAbort = rr.abortReason;
        if (!rr.errors.empty()) {
            r.recoverFirstLine = rr.errors[0].line;
            r.recoverFirstColumn = rr.errors[0].column;
        }
    }
    return r;
}

const PackedTable& packedFor(const BuiltTables& b, const string& name) {
    if (name == "SLR") return b.slrPacked;
    if (name == "LALR") return b.lalrPacked;
    if (name == "CLR") return b.clrPacked;
    return b.pgmPacked;
}

ComparisonResult compareParsers(const Grammar& g,
                                const vector<Token>& tokens,
                                const BuiltTables& tables,
                                const CompareOptions& opt) {
    ComparisonResult c;
    c.parsers.push_back(reportFor(g, tables.slr, tables.slrPacked, tables.slrMicroseconds,
                                  tokens, opt));
    c.parsers.push_back(reportFor(g, tables.lalr, tables.lalrPacked, tables.lalrMicroseconds,
                                  tokens, opt));
    c.parsers.push_back(reportFor(g, tables.clr, tables.clrPacked, tables.clrMicroseconds,
                                  tokens, opt));
    c.parsers.push_back(reportFor(g, tables.pgm, tables.pgmPacked, tables.pgmMicroseconds,
                                  tokens, opt));
    return c;
}

// Comparison table
void printComparison(ostream& os, const ComparisonResult& c) {
    ios::fmtflags saved = os.flags();
    os << left
       << setw(8) << "Parser"
       << setw(8) << "States"
       << setw(8) << "ACTION"
       << setw(7) << "GOTO"
       << setw(7) << "TOTAL"
       << setw(6) << "S/R"
       << setw(6) << "R/R"
       << setw(10) << "Result"
       << setw(8) << "Steps"
       << setw(11) << "Error step"
       << setw(11) << "Build us"
       << "Parse us\n";
    os << utilRepeat('-', 100) << "\n";
    for (size_t k = 0; k < c.parsers.size(); ++k) {
        const ParserReport& p = c.parsers[k];
        os << left
           << setw(8) << p.name
           << setw(8) << p.states
           << setw(8) << p.actionEntries
           << setw(7) << p.gotoEntries
           << setw(7) << (p.actionEntries + p.gotoEntries)
           << setw(6) << p.srConflicts
           << setw(6) << p.rrConflicts
           << setw(10) << (p.accepted ? "ACCEPTED" : "rejected")
           << setw(8) << p.steps
           << setw(11) << (p.errorStep >= 0 ? to_string(p.errorStep) : "-")
           << setw(11) << p.buildMicroseconds
           << p.parseMicroseconds << "\n";
    }
    os.flags(saved);
    for (size_t k = 0; k < c.parsers.size(); ++k) {
        const ParserReport& p = c.parsers[k];
        if (!p.accepted && p.errorLine > 0) {
            os << "  " << p.name << " error at line " << p.errorLine
               << ", column " << p.errorColumn
               << " on token '" << p.errorToken << "' after "
               << p.reductionsBeforeError << " reduce(s) on that token; expected: "
               << utilJoin(p.expected, " ") << "\n";
        }
    }
    for (size_t k = 0; k < c.parsers.size(); ++k) {
        const ParserReport& p = c.parsers[k];
        if (!p.accepted && !p.context.empty())
            os << "  " << p.name << " context: " << p.context << "\n";
    }
}
