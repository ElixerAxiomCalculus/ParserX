#include "pgm.h"

#include <deque>
#include <map>
#include <sstream>

using namespace std;

namespace {

typedef pair<int, int> Core;
typedef map<Core, set<string> > ItemSet;

struct PState {
    vector<Core> core;
    vector<set<string> > la;
    map<string, int> trans;
};

// LR(1) closure where every item carries a lookahead set
ItemSet closeItems(const Grammar& g, const FirstFollow& ff, const ItemSet& kernel) {
    ItemSet items = kernel;
    deque<Core> work;
    for (ItemSet::const_iterator it = kernel.begin(); it != kernel.end(); ++it)
        work.push_back(it->first);
    while (!work.empty()) {
        Core cur = work.front();
        work.pop_front();
        const Production& p = g.production(cur.first);
        if (cur.second >= (int)p.rhs.size()) continue;
        const string& next = p.rhs[(size_t)cur.second];
        if (!g.isNonterminal(next)) continue;
        vector<string> beta(p.rhs.begin() + cur.second + 1, p.rhs.end());
        set<string> first = ff.firstOfSequence(beta);
        set<string> la;
        for (set<string>::const_iterator f = first.begin(); f != first.end(); ++f)
            if (*f != FirstFollow::EPS) la.insert(*f);
        if (first.count(FirstFollow::EPS)) {
            const set<string>& own = items[cur];
            la.insert(own.begin(), own.end());
        }
        const vector<int>& prods = g.productionsOf(next);
        for (size_t q = 0; q < prods.size(); ++q) {
            Core fresh(prods[q], 0);
            bool existed = items.count(fresh) > 0;
            set<string>& target = items[fresh];
            size_t before = target.size();
            target.insert(la.begin(), la.end());
            if (!existed || target.size() != before) work.push_back(fresh);
        }
    }
    return items;
}

vector<string> transitionSymbols(const Grammar& g) {
    vector<string> symbols = g.terminals();
    const vector<string>& nts = g.nonterminals();
    for (size_t k = 0; k < nts.size(); ++k) symbols.push_back(nts[k]);
    return symbols;
}

ItemSet gotoKernel(const Grammar& g, const ItemSet& closed, const string& symbol) {
    ItemSet kernel;
    for (ItemSet::const_iterator it = closed.begin(); it != closed.end(); ++it) {
        const Production& p = g.production(it->first.first);
        if (it->first.second < (int)p.rhs.size() && p.rhs[(size_t)it->first.second] == symbol)
            kernel[Core(it->first.first, it->first.second + 1)] = it->second;
    }
    return kernel;
}

bool intersects(const set<string>& a, const set<string>& b) {
    for (set<string>::const_iterator it = a.begin(); it != a.end(); ++it)
        if (b.count(*it)) return true;
    return false;
}

// Pager's weak compatibility of two lookahead vectors over the same core
bool weaklyCompatible(const vector<set<string> >& a, const vector<set<string> >& b) {
    for (size_t i = 0; i < a.size(); ++i) {
        for (size_t j = i + 1; j < a.size(); ++j) {
            bool cross = intersects(a[i], b[j]) || intersects(b[i], a[j]);
            if (!cross) continue;
            if (intersects(a[i], a[j]) || intersects(b[i], b[j])) continue;
            return false;
        }
    }
    return true;
}

}

LRMachine buildPGMMachine(const Grammar& g, const FirstFollow& ff) {
    vector<PState> states;
    map<vector<Core>, vector<int> > byCore;
    deque<int> work;
    vector<char> queued;
    vector<string> symbols = transitionSymbols(g);

    PState first;
    first.core.push_back(Core(0, 0));
    first.la.push_back(set<string>());
    first.la[0].insert("$");
    states.push_back(first);
    byCore[first.core].push_back(0);
    work.push_back(0);
    queued.push_back(1);

    // Build states, merging weakly compatible ones
    while (!work.empty()) {
        int s = work.front();
        work.pop_front();
        queued[(size_t)s] = 0;
        ItemSet kernel;
        for (size_t k = 0; k < states[(size_t)s].core.size(); ++k)
            kernel[states[(size_t)s].core[k]] = states[(size_t)s].la[k];
        ItemSet closed = closeItems(g, ff, kernel);
        map<string, int> trans;
        for (size_t x = 0; x < symbols.size(); ++x) {
            ItemSet next = gotoKernel(g, closed, symbols[x]);
            if (next.empty()) continue;
            PState cand;
            for (ItemSet::const_iterator it = next.begin(); it != next.end(); ++it) {
                cand.core.push_back(it->first);
                cand.la.push_back(it->second);
            }
            int target = -1;
            vector<int>& same = byCore[cand.core];
            for (size_t q = 0; q < same.size() && target < 0; ++q) {
                PState& old = states[(size_t)same[q]];
                if (old.la == cand.la || weaklyCompatible(old.la, cand.la)) {
                    target = same[q];
                    bool grew = false;
                    for (size_t k = 0; k < old.la.size(); ++k) {
                        size_t before = old.la[k].size();
                        old.la[k].insert(cand.la[k].begin(), cand.la[k].end());
                        if (old.la[k].size() != before) grew = true;
                    }
                    if (grew && !queued[(size_t)target]) {
                        work.push_back(target);
                        queued[(size_t)target] = 1;
                    }
                }
            }
            if (target < 0) {
                target = (int)states.size();
                states.push_back(cand);
                same.push_back(target);
                work.push_back(target);
                queued.push_back(1);
            }
            trans[symbols[x]] = target;
        }
        states[(size_t)s].trans = trans;
    }

    // Keep reachable states, numbered in breadth-first order
    vector<int> renum(states.size(), -1);
    vector<int> order;
    renum[0] = 0;
    order.push_back(0);
    for (size_t k = 0; k < order.size(); ++k) {
        const map<string, int>& tr = states[(size_t)order[k]].trans;
        for (size_t x = 0; x < symbols.size(); ++x) {
            map<string, int>::const_iterator it = tr.find(symbols[x]);
            if (it == tr.end() || renum[(size_t)it->second] >= 0) continue;
            renum[(size_t)it->second] = (int)order.size();
            order.push_back(it->second);
        }
    }

    // Recompute exact lookaheads on the final automaton (fixed point)
    size_t n = order.size();
    vector<ItemSet> kernels(n);
    for (size_t k = 0; k < n; ++k)
        for (size_t i = 0; i < states[(size_t)order[k]].core.size(); ++i)
            kernels[k][states[(size_t)order[k]].core[i]] = set<string>();
    kernels[0][Core(0, 0)].insert("$");
    vector<ItemSet> closed(n);
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t k = 0; k < n; ++k) {
            closed[k] = closeItems(g, ff, kernels[k]);
            const map<string, int>& tr = states[(size_t)order[k]].trans;
            for (map<string, int>::const_iterator it = tr.begin(); it != tr.end(); ++it) {
                ItemSet next = gotoKernel(g, closed[k], it->first);
                ItemSet& target = kernels[(size_t)renum[(size_t)it->second]];
                for (ItemSet::const_iterator ni = next.begin(); ni != next.end(); ++ni) {
                    set<string>& la = target[ni->first];
                    size_t before = la.size();
                    la.insert(ni->second.begin(), ni->second.end());
                    if (la.size() != before) changed = true;
                }
            }
        }
    }

    LRMachine m;
    m.name = "PGM";
    m.kind = MachineKind::PGM;
    for (size_t k = 0; k < n; ++k) {
        State st;
        st.id = (int)k;
        for (ItemSet::const_iterator it = closed[k].begin(); it != closed[k].end(); ++it) {
            ItemRep item;
            item.prod = it->first.first;
            item.dot = it->first.second;
            item.lookaheads = it->second;
            st.items.push_back(item);
        }
        const map<string, int>& tr = states[(size_t)order[k]].trans;
        for (map<string, int>::const_iterator it = tr.begin(); it != tr.end(); ++it)
            st.transitions[it->first] = renum[(size_t)it->second];
        m.states.push_back(st);
    }
    return m;
}
