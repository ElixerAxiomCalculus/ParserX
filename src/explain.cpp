#include "explain.h"
#include "parser.h"
#include "util.h"

#include <algorithm>
#include <climits>
#include <map>
#include <set>

using namespace std;

namespace {

const int kInf = INT_MAX / 4;
const int kMaxVisitsPerState = 3;
const int kMaxCandidates = 20;
const size_t kMaxExplainedPerParser = 20;

typedef pair<int, int> Core;

vector<Core> coreOf(const vector<ItemRep>& items) {
    vector<Core> core;
    for (size_t k = 0; k < items.size(); ++k) core.push_back(Core(items[k].prod, items[k].dot));
    sort(core.begin(), core.end());
    return core;
}

string quoteList(const vector<string>& v) { return utilJoin(v, " "); }

}

// Shortest yields: a production is recorded only when it strictly improves a
// length, so the recorded choices never form a cycle
ShortestYield::ShortestYield(const Grammar& g) : g_(g) {
    for (size_t k = 0; k < g.nonterminals().size(); ++k) length_[g.nonterminals()[k]] = kInf;
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t k = 0; k < g.productions().size(); ++k) {
            const Production& p = g.productions()[k];
            int len = 0;
            for (size_t i = 0; i < p.rhs.size() && len < kInf; ++i)
                len = g.isNonterminal(p.rhs[i]) ? min(kInf, len + length_[p.rhs[i]]) : len + 1;
            if (len < length_[p.lhs]) {
                length_[p.lhs] = len;
                choice_[p.lhs] = p.id;
                changed = true;
            }
        }
    }
}

bool ShortestYield::productive(const string& symbol) const {
    if (!g_.isNonterminal(symbol)) return true;
    map<string, int>::const_iterator it = length_.find(symbol);
    return it != length_.end() && it->second < kInf;
}

vector<string> ShortestYield::expand(const vector<string>& symbols) const {
    vector<string> out;
    vector<string> work(symbols.rbegin(), symbols.rend());
    while (!work.empty()) {
        string s = work.back();
        work.pop_back();
        if (!g_.isNonterminal(s)) {
            out.push_back(s);
            continue;
        }
        map<string, int>::const_iterator c = choice_.find(s);
        if (c == choice_.end()) continue;
        const vector<string>& rhs = g_.production(c->second).rhs;
        for (size_t k = rhs.size(); k > 0; --k) work.push_back(rhs[k - 1]);
    }
    return out;
}

// FOLLOW provenance
string followChain(const Grammar& g, const FirstFollow& ff, const string& A, const string& a,
                   string* shortCause) {
    vector<string> pieces;
    vector<string> path;
    string cur = A;
    for (int guard = 0; guard < 50; ++guard) {
        path.push_back("FOLLOW(" + cur + ")");
        const FollowReason* r = ff.followReason(cur, a);
        string head = "'" + a + "' in FOLLOW(" + cur + ")";
        if (!r) {
            pieces.push_back(head);
            break;
        }
        if (r->kind == FollowReason::SEED) {
            pieces.push_back(head + " because " + cur + " is the start symbol");
            path.push_back("start symbol");
            break;
        }
        const Production& p = g.production(r->prod);
        if (r->kind == FollowReason::FIRST_OF_REST) {
            pieces.push_back(head + " because '" + a + "' can begin what follows " + cur +
                             " in " + p.toString());
            path.push_back("FIRST after " + cur + " in " + p.toString());
            break;
        }
        bool atEnd = r->position + 1 == (int)p.rhs.size();
        pieces.push_back(head + " because " + cur +
                         (atEnd ? " appears at the end of " : " is followed only by nullable symbols in ") +
                         p.toString());
        cur = p.lhs;
    }
    if (shortCause) *shortCause = utilJoin(path, " <- ");
    return utilJoin(pieces, "; ");
}

// Witness search and verification
namespace {

struct PathNode {
    int state;
    int parent;
    string symbol;
};

bool reachesConflict(const Grammar& g, const ParsingTable& t, const vector<string>& prefix,
                     int state, const string& symbol) {
    vector<Token> tokens;
    for (size_t k = 0; k <= prefix.size(); ++k) {
        Token tok;
        tok.type = k < prefix.size() ? prefix[k] : symbol;
        tok.lexeme = tok.type;
        tok.line = 1;
        tok.column = (int)k + 1;
        tokens.push_back(tok);
    }
    if (symbol != "$") {
        Token end;
        end.type = "$";
        end.lexeme = "$";
        tokens.push_back(end);
    }
    Parser p(g, t);
    ParseResult r = p.parse(tokens, true);
    for (size_t k = 0; k < r.steps.size(); ++k) {
        const ParseStep& s = r.steps[k];
        size_t current = tokens.size() - s.remaining.size();
        if (current == prefix.size() && s.stateStack.back() == state) return true;
    }
    return false;
}

}

vector<string> findWitness(const Grammar& g, const ParsingTable& t, const LRMachine& m,
                           int state, const string& symbol, bool& ok, int& tried) {
    ShortestYield yield(g);
    vector<PathNode> nodes;
    vector<int> visits(m.states.size(), 0);
    vector<int> queue;
    PathNode root = {0, -1, ""};
    nodes.push_back(root);
    queue.push_back(0);
    visits[0] = 1;
    ok = false;
    tried = 0;
    for (size_t head = 0; head < queue.size() && tried < kMaxCandidates; ++head) {
        int n = queue[head];
        if (nodes[(size_t)n].state == state) {
            vector<string> symbols;
            for (int c = n; nodes[(size_t)c].parent >= 0; c = nodes[(size_t)c].parent)
                symbols.push_back(nodes[(size_t)c].symbol);
            reverse(symbols.begin(), symbols.end());
            vector<string> prefix = yield.expand(symbols);
            ++tried;
            if (reachesConflict(g, t, prefix, state, symbol)) {
                ok = true;
                return prefix;
            }
        }
        const map<string, int>& tr = m.states[(size_t)nodes[(size_t)n].state].transitions;
        for (map<string, int>::const_iterator it = tr.begin(); it != tr.end(); ++it) {
            if (!yield.productive(it->first)) continue;
            if (visits[(size_t)it->second] >= kMaxVisitsPerState) continue;
            ++visits[(size_t)it->second];
            PathNode next = {it->second, n, it->first};
            nodes.push_back(next);
            queue.push_back((int)nodes.size() - 1);
        }
    }
    return vector<string>();
}

// --explain
void explainConflicts(ostream& os, const Grammar& g, const FirstFollow& ff, const BuiltTables& bt,
                      vector<string>& summary) {
    const ParsingTable* tables[4] = {&bt.slr, &bt.lalr, &bt.clr, &bt.pgm};
    const LRMachine* machines[4] = {&bt.slrMachine, &bt.lalrMachine, &bt.clrMachine, &bt.pgmMachine};
    map<vector<Core>, vector<int> > clrByCore;
    for (size_t s = 0; s < bt.clr.stateItems.size(); ++s)
        clrByCore[coreOf(bt.clr.stateItems[s])].push_back((int)s);

    for (size_t k = 0; k < 4; ++k) {
        const ParsingTable& t = *tables[k];
        set<pair<int, string> > cells;
        set<pair<int, string> > precCells;
        for (size_t c = 0; c < t.conflicts.size(); ++c) {
            pair<int, string> cell(t.conflicts[c].state, t.conflicts[c].symbol);
            if (t.conflicts[c].byPrecedence) precCells.insert(cell);
            else cells.insert(cell);
        }
        os << t.name << ": " << cells.size() << " conflict cell(s) to explain\n";
        if (!precCells.empty()) {
            int verified = 0;
            for (set<pair<int, string> >::const_iterator pc = precCells.begin(); pc != precCells.end(); ++pc) {
                bool ok = false;
                int tried = 0;
                findWitness(g, t, *machines[k], pc->first, pc->second, ok, tried);
                if (ok) ++verified;
            }
            os << "  " << precCells.size() << " precedence-resolved conflict cell(s): witnesses verified for "
               << verified << " (not listed)\n";
            summary.push_back("EXPLAIN " + t.name + " precedence_cells=" + to_string(precCells.size()) +
                              " verified=" + to_string(verified));
        }
        size_t shown = 0;
        for (set<pair<int, string> >::const_iterator cell = cells.begin(); cell != cells.end(); ++cell) {
            if (shown++ >= kMaxExplainedPerParser) {
                os << "  ... " << (cells.size() - kMaxExplainedPerParser) << " more\n";
                break;
            }
            int state = cell->first;
            const string& a = cell->second;
            os << "  state " << state << " on '" << a << "'\n";
            vector<string> shiftItems;
            const vector<ItemRep>& items = t.stateItems[(size_t)state];
            for (size_t i = 0; i < items.size(); ++i) {
                const Production& p = g.production(items[i].prod);
                if (items[i].dot < (int)p.rhs.size() && p.rhs[(size_t)items[i].dot] == a)
                    shiftItems.push_back(itemToString(g, items[i], MachineKind::SLR));
            }
            vector<int> reduces;
            const vector<Action>& cands = t.candidates.find(state)->second.find(a)->second;
            for (size_t q = 0; q < cands.size(); ++q)
                if (cands[q].type == ActionType::REDUCE) reduces.push_back(cands[q].value);
            vector<string> competing;
            if (!shiftItems.empty()) competing.push_back("shift '" + a + "' (" + shiftItems[0] + ")");
            for (size_t r = 0; r < reduces.size(); ++r)
                competing.push_back("reduce " + g.production(reduces[r]).toString());
            os << "    competing: " << utilJoin(competing, " vs ") << "\n";

            // Provenance
            string cause;
            if (t.kind == MachineKind::SLR) {
                for (size_t r = 0; r < reduces.size(); ++r) {
                    string shortCause;
                    os << "    FOLLOW provenance: "
                       << followChain(g, ff, g.production(reduces[r]).lhs, a, &shortCause) << "\n";
                    if (cause.empty()) cause = shortCause;
                }
            } else if (t.kind == MachineKind::LALR) {
                const vector<int>& members = clrByCore[coreOf(t.stateItems[(size_t)state])];
                os << "    merged from CLR states { " << utilJoin(utilToStrings(members), ", ") << " }\n";
                for (size_t q = 0; q < members.size(); ++q) {
                    const vector<ItemRep>& ci = bt.clr.stateItems[(size_t)members[q]];
                    os << "      CLR " << members[q] << ":";
                    for (size_t i = 0; i < ci.size(); ++i) {
                        const Production& p = g.production(ci[i].prod);
                        if (ci[i].dot != (int)p.rhs.size()) continue;
                        if (find(reduces.begin(), reduces.end(), ci[i].prod) == reduces.end()) continue;
                        vector<string> la;
                        for (set<string>::const_iterator l = ci[i].lookaheads.begin();
                             l != ci[i].lookaheads.end(); ++l)
                            la.push_back(*l == a ? "*" + *l + "*" : *l);
                        os << "  reduce " << p.toString() << " on { " << quoteList(la) << " }";
                    }
                    os << "\n";
                }
                vector<int> inherited;
                for (size_t q = 0; q < members.size(); ++q)
                    if (bt.clr.conflictCells.count(make_pair(members[q], a)))
                        inherited.push_back(members[q]);
                cause = !inherited.empty()
                            ? "inherited from CLR state(s) { " +
                                  utilJoin(utilToStrings(inherited), ", ") +
                                  " }, a conflict before merging"
                            : "created by merging CLR states { " +
                                  utilJoin(utilToStrings(members), ", ") +
                                  " }, whose lookaheads collide";
                os << "    cause: " << cause << "\n";
            } else {
                cause = "LR(1) conflict: the grammar is ambiguous or not LR(1) here";
            }

            // Witness
            bool ok = false;
            int tried = 0;
            vector<string> w = findWitness(g, t, *machines[k], state, a, ok, tried);
            string witness = ok ? utilJoin(w, " ") + (w.empty() ? "" : " ") + a : "";
            if (ok) {
                os << "    witness: " << witness << (a == "$" ? "" : " ...") << "  -- "
                   << utilJoin(competing, " vs ") << "\n";
                os << "    verified: the " << t.name << " parser is in state " << state << " with '"
                   << a << "' next after reading " << (w.empty() ? "nothing" : utilJoin(w, " "))
                   << " (" << tried << " candidate(s) tried)\n";
            } else {
                os << "    witness: not verified after " << tried << " candidate(s)\n";
            }
            summary.push_back("EXPLAIN " + t.name + " state=" + to_string(state) + " symbol='" + a +
                              "' witness=\"" + (ok ? witness : "?") + "\" verified=" +
                              (ok ? "yes" : "no") + " cause=\"" + cause + "\"");
        }
    }
}
