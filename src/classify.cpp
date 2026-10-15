#include "classify.h"

using namespace std;

namespace {

int raw(const ParsingTable& t) { return t.rawSrConflicts() + t.rawRrConflicts(); }

}

GrammarClass classifyGrammar(const Grammar& g, const FirstFollow& ff, const BuiltTables& bt) {
    LRMachine lr0 = bt.slrMachine;
    lr0.name = "LR0";
    lr0.kind = MachineKind::LR0;
    ParsingTable lr0Table = buildParsingTable(g, ff, lr0);

    GrammarClass c;
    c.lr0 = raw(lr0Table);
    c.slr = raw(bt.slr);
    c.lalr = raw(bt.lalrMerge);
    c.clr = raw(bt.clr);
    c.pgm = raw(bt.pgm);
    if (c.lr0 == 0) {
        c.name = "LR(0)";
        c.line = "CLASS LR(0)";
    } else if (c.slr == 0) {
        c.name = "SLR(1)";
        c.line = "CLASS SLR(1) not-LR(0)";
    } else if (c.lalr == 0) {
        c.name = "LALR(1)";
        c.line = "CLASS LALR(1) not-SLR(1)";
    } else if (c.clr == 0) {
        c.name = "LR(1)";
        c.line = "CLASS LR(1) not-LALR(1)";
    } else {
        c.name = "not LR(1)";
        c.line = "CLASS not-LR(1) (possibly ambiguous)";
    }
    return c;
}

void printClassification(ostream& os, const GrammarClass& c) {
    os << "Raw conflict cells before any resolution: LR(0) " << c.lr0 << ", SLR(1) " << c.slr
       << ", LALR(1) " << c.lalr << ", LR(1) " << c.clr << ", PGM " << c.pgm << "\n";
    os << "Strongest class: " << c.name << "\n";
}
