#include "regex.h"

#include <algorithm>
#include <cctype>
#include <map>

using namespace std;

namespace {

struct Frag {
    int start;
    int end;
};

// Recursive-descent regex parser that builds Thompson fragments
class RegexParser {
public:
    RegexParser(const string& pattern, Nfa& nfa) : pat_(pattern), nfa_(nfa), pos_(0) {}

    bool parse(Frag& out, string& error) {
        if (!parseAlt(out)) {
            error = error_;
            return false;
        }
        if (pos_ < pat_.size()) {
            error = "unbalanced ')' at position " + to_string(pos_ + 1);
            return false;
        }
        return true;
    }

private:
    int newState() {
        nfa_.states.push_back(NfaState());
        return (int)nfa_.states.size() - 1;
    }

    bool fail(const string& msg) {
        error_ = msg;
        return false;
    }

    bool parseAlt(Frag& out) {
        Frag left;
        if (!parseConcat(left)) return false;
        while (pos_ < pat_.size() && pat_[pos_] == '|') {
            ++pos_;
            Frag right;
            if (!parseConcat(right)) return false;
            int s = newState();
            int e = newState();
            nfa_.states[(size_t)s].eps.push_back(left.start);
            nfa_.states[(size_t)s].eps.push_back(right.start);
            nfa_.states[(size_t)left.end].eps.push_back(e);
            nfa_.states[(size_t)right.end].eps.push_back(e);
            left.start = s;
            left.end = e;
        }
        out = left;
        return true;
    }

    bool parseConcat(Frag& out) {
        int s = newState();
        Frag cur = {s, s};
        while (pos_ < pat_.size() && pat_[pos_] != '|' && pat_[pos_] != ')') {
            Frag f;
            if (!parseRepeat(f)) return false;
            nfa_.states[(size_t)cur.end].eps.push_back(f.start);
            cur.end = f.end;
        }
        out = cur;
        return true;
    }

    bool parseRepeat(Frag& out) {
        Frag f;
        if (!parseAtom(f)) return false;
        while (pos_ < pat_.size() &&
               (pat_[pos_] == '*' || pat_[pos_] == '+' || pat_[pos_] == '?')) {
            char op = pat_[pos_++];
            int s = newState();
            int e = newState();
            nfa_.states[(size_t)s].eps.push_back(f.start);
            nfa_.states[(size_t)f.end].eps.push_back(e);
            if (op == '*' || op == '?') nfa_.states[(size_t)s].eps.push_back(e);
            if (op == '*' || op == '+') nfa_.states[(size_t)f.end].eps.push_back(f.start);
            f.start = s;
            f.end = e;
        }
        out = f;
        return true;
    }

    bool parseAtom(Frag& out) {
        char c = pat_[pos_];
        if (c == '*' || c == '+' || c == '?')
            return fail(string("nothing to repeat before '") + c + "' at position " +
                        to_string(pos_ + 1));
        if (c == '(') {
            size_t open = pos_++;
            if (!parseAlt(out)) return false;
            if (pos_ >= pat_.size() || pat_[pos_] != ')')
                return fail("missing ')' for '(' at position " + to_string(open + 1));
            ++pos_;
            return true;
        }
        bitset<256> set;
        if (c == '[') {
            if (!parseClass(set)) return false;
        } else if (c == '.') {
            set.set();
            set.reset((unsigned char)'\n');
            ++pos_;
        } else if (c == '\\') {
            ++pos_;
            if (!parseEscape(set)) return false;
        } else {
            set.set((unsigned char)c);
            ++pos_;
        }
        int s = newState();
        int e = newState();
        nfa_.states[(size_t)s].chars = set;
        nfa_.states[(size_t)s].next = e;
        out.start = s;
        out.end = e;
        return true;
    }

    // Escape after the backslash; sets single chars or a predefined class
    bool parseEscape(bitset<256>& set) {
        if (pos_ >= pat_.size()) return fail("trailing backslash at end of pattern");
        char e = pat_[pos_++];
        switch (e) {
            case 't': set.set((unsigned char)'\t'); break;
            case 'n': set.set((unsigned char)'\n'); break;
            case 'r': set.set((unsigned char)'\r'); break;
            case 's':
                set.set((unsigned char)' ');
                set.set((unsigned char)'\t');
                set.set((unsigned char)'\r');
                set.set((unsigned char)'\n');
                set.set((unsigned char)'\f');
                set.set((unsigned char)'\v');
                break;
            case 'd':
                for (int k = '0'; k <= '9'; ++k) set.set((size_t)k);
                break;
            case 'w':
                for (int k = 0; k < 256; ++k)
                    if (isalnum(k) || k == '_') set.set((size_t)k);
                break;
            default: set.set((unsigned char)e); break;
        }
        return true;
    }

    bool parseClass(bitset<256>& set) {
        size_t open = pos_++;
        bool negate = false;
        if (pos_ < pat_.size() && pat_[pos_] == '^') {
            negate = true;
            ++pos_;
        }
        bool any = false;
        while (pos_ < pat_.size() && pat_[pos_] != ']') {
            int lo = -1;
            if (pat_[pos_] == '\\') {
                ++pos_;
                bitset<256> esc;
                if (!parseEscape(esc)) return false;
                if (esc.count() == 1) {
                    for (int k = 0; k < 256; ++k)
                        if (esc.test((size_t)k)) lo = k;
                } else {
                    set |= esc;
                    any = true;
                    continue;
                }
            } else {
                lo = (unsigned char)pat_[pos_++];
            }
            int hi = lo;
            if (pos_ + 1 < pat_.size() && pat_[pos_] == '-' && pat_[pos_ + 1] != ']') {
                ++pos_;
                if (pat_[pos_] == '\\') {
                    ++pos_;
                    bitset<256> esc;
                    if (!parseEscape(esc)) return false;
                    for (int k = 0; k < 256; ++k)
                        if (esc.test((size_t)k)) hi = k;
                } else {
                    hi = (unsigned char)pat_[pos_++];
                }
                if (hi < lo)
                    return fail("bad range in character class at position " + to_string(open + 1));
            }
            for (int k = lo; k <= hi; ++k) set.set((size_t)k);
            any = true;
        }
        if (pos_ >= pat_.size())
            return fail("unterminated character class starting at position " + to_string(open + 1));
        ++pos_;
        if (!any) return fail("empty character class at position " + to_string(open + 1));
        if (negate) set.flip();
        return true;
    }

    const string& pat_;
    Nfa& nfa_;
    size_t pos_;
    string error_;
};

vector<int> closure(const Nfa& nfa, const vector<int>& seed) {
    vector<char> in(nfa.states.size(), 0);
    vector<int> stack = seed;
    vector<int> out;
    for (size_t k = 0; k < seed.size(); ++k) in[(size_t)seed[k]] = 1;
    while (!stack.empty()) {
        int s = stack.back();
        stack.pop_back();
        out.push_back(s);
        const vector<int>& eps = nfa.states[(size_t)s].eps;
        for (size_t k = 0; k < eps.size(); ++k) {
            if (!in[(size_t)eps[k]]) {
                in[(size_t)eps[k]] = 1;
                stack.push_back(eps[k]);
            }
        }
    }
    sort(out.begin(), out.end());
    return out;
}

string charName(int c) {
    if (c == '\n') return "\\n";
    if (c == '\t') return "\\t";
    if (c == '\r') return "\\r";
    if (c == ' ') return "' '";
    if (c < 32 || c > 126) return "\\x" + to_string(c);
    return string(1, (char)c);
}

}

int regexToNfa(const string& pattern, int rule, Nfa& nfa, string& error) {
    if (pattern.empty()) {
        error = "empty pattern";
        return -1;
    }
    RegexParser parser(pattern, nfa);
    Frag f;
    if (!parser.parse(f, error)) return -1;
    nfa.states[(size_t)f.end].accept = rule;
    return f.start;
}

int literalToNfa(const string& text, int rule, Nfa& nfa) {
    nfa.states.push_back(NfaState());
    int start = (int)nfa.states.size() - 1;
    int cur = start;
    for (size_t k = 0; k < text.size(); ++k) {
        nfa.states.push_back(NfaState());
        int nxt = (int)nfa.states.size() - 1;
        nfa.states[(size_t)cur].chars.set((unsigned char)text[k]);
        nfa.states[(size_t)cur].next = nxt;
        cur = nxt;
    }
    nfa.states[(size_t)cur].accept = rule;
    return start;
}

bool nfaFragmentMatchesEmpty(const Nfa& nfa, int start) {
    vector<int> c = closure(nfa, vector<int>(1, start));
    for (size_t k = 0; k < c.size(); ++k)
        if (nfa.states[(size_t)c[k]].accept >= 0) return true;
    return false;
}

// Subset construction
Dfa nfaToDfa(const Nfa& nfa) {
    Dfa dfa;
    map<vector<int>, int> index;
    vector<vector<int> > sets;
    sets.push_back(closure(nfa, vector<int>(1, nfa.start)));
    index[sets[0]] = 0;
    for (size_t d = 0; d < sets.size(); ++d) {
        array<int, 256> row;
        row.fill(-1);
        int acc = -1;
        vector<int> movers;
        for (size_t k = 0; k < sets[d].size(); ++k) {
            const NfaState& ns = nfa.states[(size_t)sets[d][k]];
            if (ns.accept >= 0 && (acc < 0 || ns.accept < acc)) acc = ns.accept;
            if (ns.next >= 0) movers.push_back(sets[d][k]);
        }
        for (int c = 0; c < 256; ++c) {
            vector<int> moved;
            for (size_t k = 0; k < movers.size(); ++k) {
                const NfaState& ns = nfa.states[(size_t)movers[k]];
                if (ns.chars.test((size_t)c)) moved.push_back(ns.next);
            }
            if (moved.empty()) continue;
            vector<int> target = closure(nfa, moved);
            map<vector<int>, int>::const_iterator it = index.find(target);
            if (it == index.end()) {
                int id = (int)sets.size();
                index[target] = id;
                sets.push_back(target);
                row[(size_t)c] = id;
            } else {
                row[(size_t)c] = it->second;
            }
        }
        dfa.next.push_back(row);
        dfa.accept.push_back(acc);
    }
    dfa.start = 0;
    return dfa;
}

// Moore partition refinement
Dfa minimizeDfa(const Dfa& dfa) {
    size_t n = dfa.next.size();
    vector<int> block(n);
    map<int, int> byAccept;
    for (size_t s = 0; s < n; ++s) {
        map<int, int>::const_iterator it = byAccept.find(dfa.accept[s]);
        if (it == byAccept.end()) {
            int id = (int)byAccept.size();
            byAccept[dfa.accept[s]] = id;
            block[s] = id;
        } else {
            block[s] = it->second;
        }
    }
    size_t blocks = byAccept.size();
    while (true) {
        map<vector<int>, int> sig;
        vector<int> fresh(n);
        for (size_t s = 0; s < n; ++s) {
            vector<int> key;
            key.reserve(257);
            key.push_back(block[s]);
            for (size_t c = 0; c < 256; ++c) {
                int t = dfa.next[s][c];
                key.push_back(t < 0 ? -1 : block[(size_t)t]);
            }
            map<vector<int>, int>::const_iterator it = sig.find(key);
            if (it == sig.end()) {
                int id = (int)sig.size();
                sig[key] = id;
                fresh[s] = id;
            } else {
                fresh[s] = it->second;
            }
        }
        block = fresh;
        if (sig.size() == blocks) break;
        blocks = sig.size();
    }
    // Renumber blocks in order of first appearance, start state first
    vector<int> order(blocks, -1);
    int nextId = 0;
    order[(size_t)block[(size_t)dfa.start]] = nextId++;
    for (size_t s = 0; s < n; ++s)
        if (order[(size_t)block[s]] < 0) order[(size_t)block[s]] = nextId++;
    Dfa out;
    out.start = 0;
    out.next.resize(blocks);
    out.accept.resize(blocks);
    for (size_t s = 0; s < n; ++s) {
        int b = order[(size_t)block[s]];
        out.accept[(size_t)b] = dfa.accept[s];
        for (size_t c = 0; c < 256; ++c) {
            int t = dfa.next[s][c];
            out.next[(size_t)b][c] = t < 0 ? -1 : order[(size_t)block[(size_t)t]];
        }
    }
    return out;
}

void printDfa(ostream& os, const Dfa& dfa, const vector<string>& ruleNames) {
    os << "DFA: " << dfa.size() << " states, start " << dfa.start << "\n";
    for (int s = 0; s < dfa.size(); ++s) {
        os << "  state " << s;
        int acc = dfa.accept[(size_t)s];
        if (acc >= 0) os << "  [accepts " << ruleNames[(size_t)acc] << "]";
        os << "\n";
        map<int, vector<int> > byTarget;
        for (int c = 0; c < 256; ++c) {
            int t = dfa.next[(size_t)s][(size_t)c];
            if (t >= 0) byTarget[t].push_back(c);
        }
        for (map<int, vector<int> >::const_iterator it = byTarget.begin();
             it != byTarget.end(); ++it) {
            os << "    -> " << it->first << " on ";
            const vector<int>& cs = it->second;
            for (size_t k = 0; k < cs.size();) {
                size_t j = k;
                while (j + 1 < cs.size() && cs[j + 1] == cs[j] + 1) ++j;
                if (k) os << " ";
                os << charName(cs[k]);
                if (j > k) os << "-" << charName(cs[j]);
                k = j + 1;
            }
            os << "\n";
        }
    }
}
