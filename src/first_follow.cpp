#include "first_follow.h"
#include "util.h"

using namespace std;

const string FirstFollow::EPS = "eps";

FirstFollow::FirstFollow(const Grammar& g) : g_(g) {
    for (size_t k = 0; k < g_.terminals().size(); ++k) {
        first_[g_.terminals()[k]].insert(g_.terminals()[k]);
    }
    computeNullable();
    computeFirst();
    computeFollow();
}

const set<string>& FirstFollow::first(const string& symbol) const {
    static const set<string> kEmpty;
    map<string, set<string> >::const_iterator it = first_.find(symbol);
    if (it == first_.end()) return kEmpty;
    return it->second;
}

const set<string>& FirstFollow::follow(const string& symbol) const {
    static const set<string> kEmpty;
    map<string, set<string> >::const_iterator it = follow_.find(symbol);
    if (it == follow_.end()) return kEmpty;
    return it->second;
}

bool FirstFollow::nullable(const string& symbol) const {
    return nullable_.count(symbol) > 0;
}

// Nullable set
void FirstFollow::computeNullable() {
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t k = 0; k < g_.productions().size(); ++k) {
            const Production& p = g_.productions()[k];
            if (nullable_.count(p.lhs)) continue;
            bool all = true;
            for (size_t i = 0; i < p.rhs.size(); ++i) {
                if (!g_.isNonterminal(p.rhs[i]) || !nullable_.count(p.rhs[i])) {
                    all = false;
                    break;
                }
            }
            if (all) {
                nullable_.insert(p.lhs);
                changed = true;
            }
        }
    }
}

set<string> FirstFollow::firstOfSequence(
    const vector<string>& sequence) const {
    set<string> result;
    for (size_t k = 0; k < sequence.size(); ++k) {
        const string& sym = sequence[k];
        if (g_.isTerminal(sym)) {
            result.insert(sym);
            return result;
        }
        const set<string>& f = first(sym);
        for (set<string>::const_iterator it = f.begin(); it != f.end(); ++it) {
            if (*it != EPS) result.insert(*it);
        }
        if (!nullable(sym)) return result;
    }
    result.insert(EPS);
    return result;
}

// FIRST sets
void FirstFollow::computeFirst() {
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t k = 0; k < g_.productions().size(); ++k) {
            const Production& p = g_.productions()[k];
            set<string>& target = first_[p.lhs];
            size_t before = target.size();
            if (nullable(p.lhs)) target.insert(EPS);
            if (!p.rhs.empty()) {
                for (size_t i = 0; i < p.rhs.size(); ++i) {
                    const string& sym = p.rhs[i];
                    if (g_.isTerminal(sym)) {
                        target.insert(sym);
                        break;
                    }
                    const set<string>& f = first(sym);
                    for (set<string>::const_iterator it = f.begin(); it != f.end(); ++it) {
                        if (*it != EPS) target.insert(*it);
                    }
                    if (!nullable(sym)) break;
                }
            }
            if (target.size() != before) changed = true;
        }
    }
}

// FOLLOW sets
// The first reason each terminal entered each FOLLOW set is kept for --explain
void FirstFollow::addFollow(const string& B, const string& a, const FollowReason& why,
                            bool& changed) {
    if (!follow_[B].insert(a).second) return;
    followWhy_[B][a] = why;
    changed = true;
}

void FirstFollow::computeFollow() {
    FollowReason seed;
    seed.kind = FollowReason::SEED;
    bool changed = true;
    addFollow(g_.augmentedStart(), "$", seed, changed);
    addFollow(g_.startSymbol(), "$", seed, changed);
    while (changed) {
        changed = false;
        for (size_t k = 0; k < g_.productions().size(); ++k) {
            const Production& p = g_.productions()[k];
            for (size_t i = 0; i < p.rhs.size(); ++i) {
                const string& B = p.rhs[i];
                if (!g_.isNonterminal(B)) continue;
                vector<string> rest(p.rhs.begin() + (i + 1), p.rhs.end());
                set<string> f = firstOfSequence(rest);
                FollowReason why;
                why.prod = p.id;
                why.position = (int)i;
                why.kind = FollowReason::FIRST_OF_REST;
                for (set<string>::const_iterator it = f.begin(); it != f.end(); ++it) {
                    if (*it != EPS) addFollow(B, *it, why, changed);
                }
                if (f.count(EPS) > 0) {
                    why.kind = FollowReason::FOLLOW_OF_LHS;
                    vector<string> fl(follow(p.lhs).begin(), follow(p.lhs).end());
                    for (size_t t = 0; t < fl.size(); ++t) addFollow(B, fl[t], why, changed);
                }
            }
        }
    }
}

const FollowReason* FirstFollow::followReason(const string& symbol, const string& terminal) const {
    map<string, map<string, FollowReason> >::const_iterator s = followWhy_.find(symbol);
    if (s == followWhy_.end()) return 0;
    map<string, FollowReason>::const_iterator t = s->second.find(terminal);
    return t == s->second.end() ? 0 : &t->second;
}

void FirstFollow::printFirst(ostream& os) const {
    os << "FIRST sets (computed by fixed-point iteration):\n";
    for (size_t k = 0; k < g_.nonterminals().size(); ++k) {
        const string& nt = g_.nonterminals()[k];
        os << "  FIRST(" << nt << ") = " << utilFormatSet(first(nt)) << "\n";
    }
}

void FirstFollow::printFollow(ostream& os) const {
    os << "FOLLOW sets (computed by fixed-point iteration):\n";
    for (size_t k = 0; k < g_.nonterminals().size(); ++k) {
        const string& nt = g_.nonterminals()[k];
        os << "  FOLLOW(" << nt << ") = " << utilFormatSet(follow(nt)) << "\n";
    }
}
