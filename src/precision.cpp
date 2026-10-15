#include "precision.h"
#include "util.h"

#include <algorithm>
#include <map>
#include <set>

using namespace std;

namespace {

typedef pair<int, int> Core;

vector<Core> coreOf(const vector<ItemRep>& items) {
    vector<Core> core;
    for (size_t k = 0; k < items.size(); ++k) core.push_back(Core(items[k].prod, items[k].dot));
    sort(core.begin(), core.end());
    return core;
}

vector<int> mapByCore(const ParsingTable& from, const ParsingTable& to) {
    map<vector<Core>, int> index;
    for (size_t s = 0; s < to.stateItems.size(); ++s) index[coreOf(to.stateItems[s])] = (int)s;
    vector<int> out(from.stateItems.size(), -1);
    for (size_t s = 0; s < from.stateItems.size(); ++s) {
        map<vector<Core>, int>::const_iterator it = index.find(coreOf(from.stateItems[s]));
        if (it != index.end()) out[s] = it->second;
    }
    return out;
}

// Walk both automata from their start states with the same symbols
vector<int> mapByPaths(const LRMachine& from, const LRMachine& to) {
    vector<int> out(from.states.size(), -1);
    vector<pair<int, int> > queue(1, make_pair(0, 0));
    out[0] = 0;
    for (size_t h = 0; h < queue.size(); ++h) {
        const map<string, int>& tf = from.states[(size_t)queue[h].first].transitions;
        const map<string, int>& tt = to.states[(size_t)queue[h].second].transitions;
        for (map<string, int>::const_iterator it = tf.begin(); it != tf.end(); ++it) {
            map<string, int>::const_iterator other = tt.find(it->first);
            if (other == tt.end() || out[(size_t)it->second] >= 0) continue;
            out[(size_t)it->second] = other->second;
            queue.push_back(make_pair(it->second, other->second));
        }
    }
    return out;
}

const set<string>* lookaheadsOf(const vector<ItemRep>& items, int prod, int dot) {
    for (size_t k = 0; k < items.size(); ++k)
        if (items[k].prod == prod && items[k].dot == dot) return &items[k].lookaheads;
    return 0;
}

long extra(const set<string>& have, const set<string>& exact) {
    long n = 0;
    for (set<string>::const_iterator it = have.begin(); it != have.end(); ++it)
        if (!exact.count(*it)) ++n;
    return n;
}

int rowSize(const ParsingTable& t, int state) {
    map<int, map<string, Action> >::const_iterator row = t.action.find(state);
    return row == t.action.end() ? 0 : (int)row->second.size();
}

// Raw reduce entries (state, terminal, production) before conflict resolution
set<pair<pair<int, string>, int> > rawReduces(const ParsingTable& t, const vector<int>* remap) {
    set<pair<pair<int, string>, int> > out;
    map<int, map<string, vector<Action> > >::const_iterator row;
    for (row = t.candidates.begin(); row != t.candidates.end(); ++row) {
        int s = remap ? (*remap)[(size_t)row->first] : row->first;
        map<string, vector<Action> >::const_iterator cell;
        for (cell = row->second.begin(); cell != row->second.end(); ++cell)
            for (size_t k = 0; k < cell->second.size(); ++k)
                if (cell->second[k].type == ActionType::REDUCE)
                    out.insert(make_pair(make_pair(s, cell->first), cell->second[k].value));
    }
    return out;
}

}

PrecisionReport computePrecision(const Grammar& g, const FirstFollow& ff, const BuiltTables& bt) {
    PrecisionReport r;
    const ParsingTable& clr = bt.clr;
    vector<int> toSlr = mapByCore(clr, bt.slr);
    vector<int> toLalr = mapByCore(clr, bt.lalr);
    vector<int> toPgm = mapByPaths(bt.clrMachine, bt.pgmMachine);
    long inflSlr = 0;
    long inflLalr = 0;
    long inflPgm = 0;
    for (size_t c = 0; c < clr.stateItems.size(); ++c) {
        const vector<ItemRep>& items = clr.stateItems[c];
        for (size_t i = 0; i < items.size(); ++i) {
            const Production& p = g.production(items[i].prod);
            if (items[i].dot != (int)p.rhs.size() || p.lhs == g.augmentedStart()) continue;
            const set<string>& exact = items[i].lookaheads;
            r.spuriousSlr += extra(ff.follow(p.lhs), exact);
            const set<string>* la = toLalr[c] < 0 ? 0
                : lookaheadsOf(bt.lalr.stateItems[(size_t)toLalr[c]], items[i].prod, items[i].dot);
            if (la) r.spuriousLalr += extra(*la, exact);
            const set<string>* lp = toPgm[c] < 0 ? 0
                : lookaheadsOf(bt.pgm.stateItems[(size_t)toPgm[c]], items[i].prod, items[i].dot);
            if (lp) r.spuriousPgm += extra(*lp, exact);
        }
        int base = rowSize(clr, (int)c);
        if (toSlr[c] >= 0) inflSlr += rowSize(bt.slr, toSlr[c]) - base;
        if (toLalr[c] >= 0) inflLalr += rowSize(bt.lalr, toLalr[c]) - base;
        if (toPgm[c] >= 0) inflPgm += rowSize(bt.pgm, toPgm[c]) - base;
    }
    double n = clr.stateItems.empty() ? 1.0 : (double)clr.stateItems.size();
    r.inflationSlr = inflSlr / n;
    r.inflationLalr = inflLalr / n;
    r.inflationPgm = inflPgm / n;

    // SLR reduce cells that LALR does not have (states matched by core)
    vector<int> slrToLalr = mapByCore(bt.slr, bt.lalr);
    set<pair<pair<int, string>, int> > slrCells = rawReduces(bt.slr, &slrToLalr);
    set<pair<pair<int, string>, int> > lalrCells = rawReduces(bt.lalr, 0);
    for (set<pair<pair<int, string>, int> >::const_iterator it = slrCells.begin(); it != slrCells.end(); ++it)
        if (!lalrCells.count(*it)) ++r.spuriousReduceSlrVsLalr;
    return r;
}

void printPrecision(ostream& os, const PrecisionReport& p) {
    os << "Spurious reduce lookaheads compared with CLR (summed over CLR states and completed items):\n"
       << "  SLR  " << p.spuriousSlr << "  (FOLLOW sets)\n"
       << "  LALR " << p.spuriousLalr << "  (merged lookaheads)\n"
       << "  PGM  " << p.spuriousPgm << "  (weakly compatible merges)\n"
       << "  CLR  0  (reference)\n";
    os << "SLR reduce cells absent from the LALR table: " << p.spuriousReduceSlrVsLalr << "\n";
    os << "Mean extra filled ACTION cells per CLR state (expected-set inflation): SLR "
       << utilFixed3(p.inflationSlr) << ", LALR " << utilFixed3(p.inflationLalr) << ", PGM "
       << utilFixed3(p.inflationPgm) << "\n";
}

string precisionLine(const PrecisionReport& p) {
    return "PRECISION SLR=" + to_string(p.spuriousSlr) + " LALR=" + to_string(p.spuriousLalr) +
           " PGM=" + to_string(p.spuriousPgm) + " CLR=0 spurious_reduce_slr_vs_lalr=" +
           to_string(p.spuriousReduceSlrVsLalr) + " inflation_slr=" + utilFixed3(p.inflationSlr) +
           " inflation_lalr=" + utilFixed3(p.inflationLalr) + " inflation_pgm=" +
           utilFixed3(p.inflationPgm);
}
