#include "lalr_dp.h"

#include <algorithm>
#include <climits>
#include <map>
#include <sstream>

using namespace std;

namespace {

// Recursive traversal of the digraph algorithm
struct Digraph {
    const vector<vector<int> >& rel;
    vector<vector<char> >& F;
    vector<int> N;
    vector<int> stack;
    int sccs;

    Digraph(const vector<vector<int> >& r, vector<vector<char> >& f)
        : rel(r), F(f), N(r.size(), 0), sccs(0) {}

    static void unite(vector<char>& into, const vector<char>& from) {
        for (size_t k = 0; k < into.size(); ++k)
            if (from[k]) into[k] = 1;
    }

    void traverse(int x) {
        stack.push_back(x);
        int d = (int)stack.size();
        N[(size_t)x] = d;
        const vector<int>& succ = rel[(size_t)x];
        for (size_t k = 0; k < succ.size(); ++k) {
            int y = succ[k];
            if (N[(size_t)y] == 0) traverse(y);
            N[(size_t)x] = min(N[(size_t)x], N[(size_t)y]);
            unite(F[(size_t)x], F[(size_t)y]);
        }
        if (N[(size_t)x] == d) {
            int members = 0;
            while (true) {
                int top = stack.back();
                stack.pop_back();
                N[(size_t)top] = INT_MAX;
                if (top != x) F[(size_t)top] = F[(size_t)x];
                ++members;
                if (top == x) break;
            }
            if (members > 1) ++sccs;
        }
    }
};

typedef pair<int, int> Core;

vector<Core> coreOfItems(const vector<ItemRep>& items) {
    vector<Core> core;
    for (size_t k = 0; k < items.size(); ++k)
        core.push_back(Core(items[k].prod, items[k].dot));
    sort(core.begin(), core.end());
    return core;
}

}

void digraph(const vector<vector<int> >& relation, vector<vector<char> >& sets,
             int* nontrivialSccs) {
    Digraph d(relation, sets);
    for (size_t x = 0; x < relation.size(); ++x)
        if (d.N[x] == 0) d.traverse((int)x);
    if (nontrivialSccs) *nontrivialSccs += d.sccs;
}

LRMachine buildLALRMachineDP(const Grammar& g, const FirstFollow& ff,
                             const LRMachine& lr0, DPStats* stats) {
    const vector<string>& terms = g.terminals();
    map<string, int> termIndex;
    for (size_t k = 0; k < terms.size(); ++k) termIndex[terms[k]] = (int)k;

    // Non-terminal transitions (p, A)
    vector<pair<int, string> > trans;
    map<pair<int, string>, int> transIndex;
    for (size_t p = 0; p < lr0.states.size(); ++p) {
        map<string, int>::const_iterator tr;
        for (tr = lr0.states[p].transitions.begin(); tr != lr0.states[p].transitions.end(); ++tr) {
            if (!g.isNonterminal(tr->first)) continue;
            transIndex[make_pair((int)p, tr->first)] = (int)trans.size();
            trans.push_back(make_pair((int)p, tr->first));
        }
    }
    size_t n = trans.size();
    DPStats st;
    st.ntTransitions = (int)n;

    // DR and reads
    vector<vector<char> > sets(n, vector<char>(terms.size(), 0));
    vector<vector<int> > reads(n);
    for (size_t x = 0; x < n; ++x) {
        int r = lr0.states[(size_t)trans[x].first].transitions.find(trans[x].second)->second;
        const State& target = lr0.states[(size_t)r];
        map<string, int>::const_iterator tr;
        for (tr = target.transitions.begin(); tr != target.transitions.end(); ++tr) {
            if (g.isTerminal(tr->first)) {
                sets[x][(size_t)termIndex[tr->first]] = 1;
            } else if (ff.nullable(tr->first)) {
                reads[x].push_back(transIndex[make_pair(r, tr->first)]);
                ++st.readsEdges;
            }
        }
        for (size_t i = 0; i < target.items.size(); ++i) {
            const Production& p = g.production(target.items[i].prod);
            if (p.lhs == g.augmentedStart() && target.items[i].dot == (int)p.rhs.size())
                sets[x][(size_t)termIndex["$"]] = 1;
        }
    }
    digraph(reads, sets, &st.nontrivialSccs);

    // includes and lookback
    vector<vector<int> > includes(n);
    map<pair<int, int>, vector<int> > lookback;
    for (size_t x = 0; x < n; ++x) {
        int pStart = trans[x].first;
        const vector<int>& prods = g.productionsOf(trans[x].second);
        for (size_t q = 0; q < prods.size(); ++q) {
            const Production& prod = g.production(prods[q]);
            int p = pStart;
            for (size_t i = 0; i < prod.rhs.size(); ++i) {
                const string& sym = prod.rhs[i];
                if (g.isNonterminal(sym)) {
                    bool restNullable = true;
                    for (size_t j = i + 1; j < prod.rhs.size() && restNullable; ++j)
                        restNullable = g.isNonterminal(prod.rhs[j]) && ff.nullable(prod.rhs[j]);
                    if (restNullable) {
                        includes[(size_t)transIndex[make_pair(p, sym)]].push_back((int)x);
                        ++st.includesEdges;
                    }
                }
                p = lr0.states[(size_t)p].transitions.find(sym)->second;
            }
            lookback[make_pair(p, prods[q])].push_back((int)x);
            ++st.lookbackEdges;
        }
    }
    digraph(includes, sets, &st.nontrivialSccs);

    // LA(q, A -> w) = union of Follow(p, A) over lookback
    LRMachine m;
    m.name = "LALR";
    m.kind = MachineKind::LALR;
    for (size_t s = 0; s < lr0.states.size(); ++s) {
        State state = lr0.states[s];
        ostringstream note;
        note << "LR(0) state " << s;
        state.note = note.str();
        for (size_t i = 0; i < state.items.size(); ++i) {
            ItemRep& item = state.items[i];
            const Production& p = g.production(item.prod);
            if (item.dot != (int)p.rhs.size() || p.lhs == g.augmentedStart()) continue;
            map<pair<int, int>, vector<int> >::const_iterator lb =
                lookback.find(make_pair((int)s, item.prod));
            if (lb == lookback.end()) continue;
            for (size_t k = 0; k < lb->second.size(); ++k) {
                const vector<char>& f = sets[(size_t)lb->second[k]];
                for (size_t t = 0; t < f.size(); ++t)
                    if (f[t]) item.lookaheads.insert(terms[t]);
            }
        }
        m.states.push_back(state);
    }
    if (stats) *stats = st;
    return m;
}

// Verification: identical tables up to renumbering through LR(0) cores
bool verifyLalrTables(const Grammar& g, const ParsingTable& merge,
                      const ParsingTable& dp, ostream& os) {
    int diffs = 0;
    const int kShow = 20;
    if (merge.stateItems.size() != dp.stateItems.size()) {
        os << "VERIFY LALR DIFF state count: merge=" << merge.stateItems.size()
           << " dp=" << dp.stateItems.size() << "\n";
        ++diffs;
    }
    map<vector<Core>, int> dpByCore;
    for (size_t s = 0; s < dp.stateItems.size(); ++s)
        dpByCore[coreOfItems(dp.stateItems[s])] = (int)s;
    vector<int> toDp(merge.stateItems.size(), -1);
    for (size_t s = 0; s < merge.stateItems.size(); ++s) {
        map<vector<Core>, int>::const_iterator it = dpByCore.find(coreOfItems(merge.stateItems[s]));
        if (it == dpByCore.end()) {
            if (diffs++ < kShow)
                os << "VERIFY LALR DIFF merge state " << s << " has no LR(0) state with its core\n";
        } else {
            toDp[s] = it->second;
        }
    }
    int cells = 0;
    for (size_t s = 0; s < merge.stateItems.size(); ++s) {
        int d = toDp[s];
        if (d < 0) continue;
        const vector<string>& terms = g.terminals();
        for (size_t k = 0; k < terms.size(); ++k) {
            set<string> a;
            set<string> b;
            map<int, map<string, vector<Action> > >::const_iterator row = merge.candidates.find((int)s);
            if (row != merge.candidates.end() && row->second.count(terms[k])) {
                const vector<Action>& acts = row->second.find(terms[k])->second;
                for (size_t q = 0; q < acts.size(); ++q) {
                    Action mapped = acts[q];
                    if (mapped.type == ActionType::SHIFT) mapped.value = toDp[(size_t)mapped.value];
                    a.insert(mapped.toString(g));
                }
            }
            row = dp.candidates.find(d);
            if (row != dp.candidates.end() && row->second.count(terms[k])) {
                const vector<Action>& acts = row->second.find(terms[k])->second;
                for (size_t q = 0; q < acts.size(); ++q) b.insert(acts[q].toString(g));
            }
            if (!a.empty() || !b.empty()) ++cells;
            if (a != b && diffs++ < kShow) {
                vector<string> va(a.begin(), a.end());
                vector<string> vb(b.begin(), b.end());
                os << "VERIFY LALR DIFF merge state " << s << " / dp state " << d << " on '"
                   << terms[k] << "': merge={" << (va.empty() ? "" : va[0])
                   << (va.size() > 1 ? ", ..." : "") << "} dp={" << (vb.empty() ? "" : vb[0])
                   << (vb.size() > 1 ? ", ..." : "") << "}\n";
            }
        }
        map<int, map<string, int> >::const_iterator gm = merge.gotos.find((int)s);
        map<int, map<string, int> >::const_iterator gd = dp.gotos.find(d);
        map<string, int> mappedGotos;
        if (gm != merge.gotos.end()) {
            map<string, int>::const_iterator it;
            for (it = gm->second.begin(); it != gm->second.end(); ++it)
                mappedGotos[it->first] = toDp[(size_t)it->second];
        }
        map<string, int> dpGotos;
        if (gd != dp.gotos.end()) dpGotos = gd->second;
        cells += (int)mappedGotos.size();
        if (mappedGotos != dpGotos && diffs++ < kShow)
            os << "VERIFY LALR DIFF GOTO row of merge state " << s << " / dp state " << d << "\n";
    }
    if (diffs == 0) {
        os << "VERIFY LALR dp==merge OK (" << merge.stateItems.size() << " states, " << cells
           << " ACTION/GOTO cells compared)\n";
        return true;
    }
    os << "VERIFY LALR dp==merge FAILED (" << diffs << " differences)\n";
    return false;
}
