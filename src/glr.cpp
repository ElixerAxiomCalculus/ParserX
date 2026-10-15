#include "glr.h"
#include "util.h"

#include <map>
#include <tuple>

using namespace std;

namespace {

const long kMaxWork = 5000000;
const size_t kMaxNodes = 200000;
const unsigned long long kCap = 1000000000000000000ULL;
const size_t kShowAmbiguities = 10;

struct GssEdge {
    int to;
    int sppf;
};

struct GssNode {
    int state;
    int level;
    vector<GssEdge> edges;
};

struct Alt {
    int prod;
    vector<int> kids;
};

struct SppfNode {
    string symbol;
    int start;
    int end;
    vector<Alt> alts;
};

struct Overflow {
    string reason;
};

class Glr {
public:
    Glr(const Grammar& g, const ParsingTable& t, const vector<Token>& tokens, GlrResult& r)
        : g_(g), t_(t), tokens_(tokens), r_(r), work_(0) {}

    void run() {
        int root = newNode(0, 0);
        vector<int> tops(1, root);
        for (size_t i = 0; i < tokens_.size(); ++i) {
            const string& a = tokens_[i].type;
            reduceAll(tops, (int)i, a);
            if (a == "$") {
                for (size_t k = 0; k < tops.size() && !r_.accepted; ++k) {
                    if (!has(gss_[(size_t)tops[k]].state, a, ActionType::ACCEPT)) continue;
                    const vector<GssEdge>& e = gss_[(size_t)tops[k]].edges;
                    for (size_t q = 0; q < e.size(); ++q) {
                        if (e[q].to != root) continue;
                        r_.accepted = true;
                        count(e[q].sppf);
                    }
                }
                if (r_.accepted) return;
            }
            vector<int> next = shiftAll(tops, (int)i, a);
            if (next.empty()) {
                r_.errorToken = (int)i;
                return;
            }
            tops = next;
        }
    }

    void summarise() {
        r_.gssNodes = (int)gss_.size();
        for (size_t k = 0; k < gss_.size(); ++k) r_.gssEdges += (int)gss_[k].edges.size();
        r_.sppfNodes = (int)sppf_.size();
        for (size_t k = 0; k < sppf_.size(); ++k) {
            r_.packedAlternatives += (int)sppf_[k].alts.size();
            if (sppf_[k].alts.size() > 1 && r_.ambiguities.size() < kShowAmbiguities)
                r_.ambiguities.push_back(sppf_[k].symbol + " over tokens " + to_string(sppf_[k].start) +
                                         ".." + to_string(sppf_[k].end - 1) + " (" +
                                         to_string(sppf_[k].alts.size()) + " alternatives)");
        }
    }

private:
    int newNode(int state, int level) {
        if (gss_.size() >= kMaxNodes) throw Overflow{"graph-structured stack too large"};
        GssNode n;
        n.state = state;
        n.level = level;
        gss_.push_back(n);
        return (int)gss_.size() - 1;
    }

    int sppfFor(const string& symbol, int start, int end) {
        tuple<string, int, int> key(symbol, start, end);
        map<tuple<string, int, int>, int>::const_iterator it = sppfIndex_.find(key);
        if (it != sppfIndex_.end()) return it->second;
        if (sppf_.size() >= kMaxNodes) throw Overflow{"parse forest too large"};
        SppfNode n;
        n.symbol = symbol;
        n.start = start;
        n.end = end;
        sppf_.push_back(n);
        sppfIndex_[key] = (int)sppf_.size() - 1;
        return (int)sppf_.size() - 1;
    }

    void addAlt(int node, int prod, const vector<int>& kids) {
        vector<Alt>& alts = sppf_[(size_t)node].alts;
        for (size_t k = 0; k < alts.size(); ++k)
            if (alts[k].prod == prod && alts[k].kids == kids) return;
        Alt a;
        a.prod = prod;
        a.kids = kids;
        alts.push_back(a);
    }

    bool has(int state, const string& a, ActionType type) const {
        map<int, map<string, vector<Action> > >::const_iterator row = t_.candidates.find(state);
        if (row == t_.candidates.end()) return false;
        map<string, vector<Action> >::const_iterator cell = row->second.find(a);
        if (cell == row->second.end()) return false;
        for (size_t k = 0; k < cell->second.size(); ++k)
            if (cell->second[k].type == type) return true;
        return false;
    }

    const vector<Action>* actions(int state, const string& a) const {
        map<int, map<string, vector<Action> > >::const_iterator row = t_.candidates.find(state);
        if (row == t_.candidates.end()) return 0;
        map<string, vector<Action> >::const_iterator cell = row->second.find(a);
        return cell == row->second.end() ? 0 : &cell->second;
    }

    int gotoOf(int state, const string& nt) const {
        map<int, map<string, int> >::const_iterator row = t_.gotos.find(state);
        if (row == t_.gotos.end()) return -1;
        map<string, int>::const_iterator it = row->second.find(nt);
        return it == row->second.end() ? -1 : it->second;
    }

    // Every path of m edges down from v, with the forest nodes it passes
    void paths(int v, int m, vector<int>& kids, vector<pair<int, vector<int> > >& out) {
        if (++work_ > kMaxWork) throw Overflow{"work limit reached"};
        if (m == 0) {
            out.push_back(make_pair(v, vector<int>(kids.rbegin(), kids.rend())));
            return;
        }
        const vector<GssEdge> edges = gss_[(size_t)v].edges;
        for (size_t k = 0; k < edges.size(); ++k) {
            kids.push_back(edges[k].sppf);
            paths(edges[k].to, m - 1, kids, out);
            kids.pop_back();
        }
    }

    bool hasEdge(int from, int to) const {
        const vector<GssEdge>& e = gss_[(size_t)from].edges;
        for (size_t k = 0; k < e.size(); ++k)
            if (e[k].to == to) return true;
        return false;
    }

    // Reductions at one level, repeated until nothing new appears
    void reduceAll(vector<int>& tops, int level, const string& a) {
        bool changed = true;
        while (changed) {
            changed = false;
            for (size_t k = 0; k < tops.size(); ++k) {
                const vector<Action>* acts = actions(gss_[(size_t)tops[k]].state, a);
                if (!acts) continue;
                for (size_t q = 0; q < acts->size(); ++q) {
                    if ((*acts)[q].type != ActionType::REDUCE) continue;
                    const Production& p = g_.production((*acts)[q].value);
                    vector<int> kids;
                    vector<pair<int, vector<int> > > found;
                    paths(tops[k], (int)p.rhs.size(), kids, found);
                    for (size_t f = 0; f < found.size(); ++f) {
                        int u = found[f].first;
                        int target = gotoOf(gss_[(size_t)u].state, p.lhs);
                        if (target < 0) continue;
                        int node = sppfFor(p.lhs, gss_[(size_t)u].level, level);
                        addAlt(node, p.id, found[f].second);
                        int w = -1;
                        for (size_t c = 0; c < tops.size(); ++c)
                            if (gss_[(size_t)tops[c]].state == target) w = tops[c];
                        if (w < 0) {
                            w = newNode(target, level);
                            tops.push_back(w);
                            changed = true;
                        }
                        if (!hasEdge(w, u)) {
                            GssEdge e = {u, node};
                            gss_[(size_t)w].edges.push_back(e);
                            changed = true;
                        }
                    }
                }
            }
        }
    }

    vector<int> shiftAll(const vector<int>& tops, int level, const string& a) {
        vector<int> next;
        map<int, int> byState;
        int leaf = -1;
        for (size_t k = 0; k < tops.size(); ++k) {
            const vector<Action>* acts = actions(gss_[(size_t)tops[k]].state, a);
            if (!acts) continue;
            for (size_t q = 0; q < acts->size(); ++q) {
                if ((*acts)[q].type != ActionType::SHIFT) continue;
                if (leaf < 0) leaf = sppfFor(a, level, level + 1);
                int s = (*acts)[q].value;
                map<int, int>::const_iterator it = byState.find(s);
                int w;
                if (it == byState.end()) {
                    w = newNode(s, level + 1);
                    byState[s] = w;
                    next.push_back(w);
                } else {
                    w = it->second;
                }
                if (!hasEdge(w, tops[k])) {
                    GssEdge e = {tops[k], leaf};
                    gss_[(size_t)w].edges.push_back(e);
                }
            }
        }
        return next;
    }

    // Number of distinct trees below a forest node (saturating)
    unsigned long long countNode(int n, vector<int>& mark, vector<unsigned long long>& memo) {
        if (mark[(size_t)n] == 2) return memo[(size_t)n];
        if (mark[(size_t)n] == 1) {
            r_.infinite = true;
            return 0;
        }
        const SppfNode& node = sppf_[(size_t)n];
        if (node.alts.empty()) {
            mark[(size_t)n] = 2;
            memo[(size_t)n] = 1;
            return 1;
        }
        mark[(size_t)n] = 1;
        unsigned long long total = 0;
        for (size_t k = 0; k < node.alts.size(); ++k) {
            unsigned long long product = 1;
            for (size_t c = 0; c < node.alts[k].kids.size(); ++c) {
                unsigned long long sub = countNode(node.alts[k].kids[c], mark, memo);
                product = (sub != 0 && product > kCap / sub) ? kCap : product * sub;
            }
            total = total > kCap - product ? kCap : total + product;
        }
        if (total >= kCap) r_.saturated = true;
        mark[(size_t)n] = 2;
        memo[(size_t)n] = total;
        return total;
    }

    void count(int root) {
        vector<int> mark(sppf_.size(), 0);
        vector<unsigned long long> memo(sppf_.size(), 0);
        r_.parses = countNode(root, mark, memo);
    }

    const Grammar& g_;
    const ParsingTable& t_;
    const vector<Token>& tokens_;
    GlrResult& r_;
    long work_;
    vector<GssNode> gss_;
    vector<SppfNode> sppf_;
    map<tuple<string, int, int>, int> sppfIndex_;
};

}

GlrResult glrParse(const Grammar& g, const ParsingTable& t, const vector<Token>& tokens) {
    GlrResult r;
    Glr glr(g, t, tokens, r);
    try {
        glr.run();
    } catch (const Overflow& o) {
        r.aborted = true;
        r.abortReason = o.reason;
        r.accepted = false;
    }
    glr.summarise();
    return r;
}

void printGlr(ostream& os, const GlrResult& r) {
    os << "Graph-structured stack: " << r.gssNodes << " nodes, " << r.gssEdges << " edges; parse forest: "
       << r.sppfNodes << " nodes, " << r.packedAlternatives << " packed alternatives\n";
    if (r.aborted) {
        os << "Stopped: " << r.abortReason << "\n";
        return;
    }
    if (!r.accepted) {
        os << "Rejected: no stack can continue at token " << r.errorToken << "\n";
        return;
    }
    os << "Accepted with ";
    if (r.infinite) os << "infinitely many parses (cyclic forest)\n";
    else os << (r.saturated ? "at least " : "") << r.parses << " distinct parse(s)\n";
    for (size_t k = 0; k < r.ambiguities.size(); ++k) os << "  ambiguous " << r.ambiguities[k] << "\n";
}
