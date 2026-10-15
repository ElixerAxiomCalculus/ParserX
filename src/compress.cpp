#include "compress.h"

#include <algorithm>
#include <climits>
#include <set>

using namespace std;

namespace {

typedef vector<pair<int, int> > Row;

// First-fit comb packing of rows (column, value); returns base of each row
vector<int> packRows(const vector<Row>& rows, vector<int>& next, vector<int>& check) {
    vector<int> base(rows.size(), 0);
    for (size_t r = 0; r < rows.size(); ++r) {
        if (rows[r].empty()) continue;
        for (int b = 0;; ++b) {
            bool fits = true;
            for (size_t k = 0; k < rows[r].size() && fits; ++k) {
                size_t idx = (size_t)(b + rows[r][k].first);
                if (idx < check.size() && check[idx] >= 0) fits = false;
            }
            if (!fits) continue;
            base[r] = b;
            for (size_t k = 0; k < rows[r].size(); ++k) {
                size_t idx = (size_t)(b + rows[r][k].first);
                if (idx >= check.size()) {
                    check.resize(idx + 1, -1);
                    next.resize(idx + 1, 0);
                }
                check[idx] = (int)r;
                next[idx] = rows[r][k].second;
            }
            break;
        }
    }
    return base;
}

// Share identical rows; returns the unique rows and the row index of each state
vector<Row> shareRows(const vector<Row>& rows, const vector<int>& defaults,
                      vector<int>& rowOf, vector<int>& uniqueDefaults) {
    map<pair<Row, int>, int> seen;
    vector<Row> unique;
    rowOf.assign(rows.size(), 0);
    for (size_t s = 0; s < rows.size(); ++s) {
        pair<Row, int> key(rows[s], defaults[s]);
        map<pair<Row, int>, int>::const_iterator it = seen.find(key);
        if (it == seen.end()) {
            int id = (int)unique.size();
            seen[key] = id;
            unique.push_back(rows[s]);
            uniqueDefaults.push_back(defaults[s]);
            rowOf[s] = id;
        } else {
            rowOf[s] = it->second;
        }
    }
    return unique;
}

int lookup(const vector<int>& base, const vector<int>& next, const vector<int>& check,
           int row, int col) {
    size_t idx = (size_t)(base[(size_t)row] + col);
    if (idx < check.size() && check[idx] == row) return next[idx];
    return INT_MIN;
}

}

// 0 = error, s+1 = shift s, -(p+1) = reduce p, reduce 0 = accept
int PackedTable::encode(const Action& a) {
    if (a.type == ActionType::SHIFT) return a.value + 1;
    if (a.type == ActionType::REDUCE) return -(a.value + 1);
    if (a.type == ActionType::ACCEPT) return -1;
    return 0;
}

Action PackedTable::decode(int v) {
    Action a;
    if (v > 0) {
        a.type = ActionType::SHIFT;
        a.value = v - 1;
    } else if (v == -1) {
        a.type = ActionType::ACCEPT;
        a.value = 0;
    } else {
        a.type = ActionType::REDUCE;
        a.value = -v - 1;
    }
    return a;
}

void PackedTable::build(const Grammar& g, const ParsingTable& t) {
    terms_ = g.terminals();
    nts_.clear();
    for (size_t k = 0; k < g.nonterminals().size(); ++k)
        if (g.nonterminals()[k] != g.augmentedStart()) nts_.push_back(g.nonterminals()[k]);
    for (size_t k = 0; k < terms_.size(); ++k) termIndex_[terms_[k]] = (int)k;
    for (size_t k = 0; k < nts_.size(); ++k) ntIndex_[nts_[k]] = (int)k;
    size_t states = t.stateItems.size();

    // ACTION rows with default reductions removed
    vector<Row> rows(states);
    vector<int> defaults(states, 0);
    defaultRows_ = 0;
    for (size_t s = 0; s < states; ++s) {
        map<int, map<string, Action> >::const_iterator row = t.action.find((int)s);
        if (row == t.action.end()) continue;
        map<int, int> reduceCount;
        for (map<string, Action>::const_iterator c = row->second.begin(); c != row->second.end(); ++c)
            if (c->second.type == ActionType::REDUCE) ++reduceCount[c->second.value];
        int best = -1;
        int bestCount = 0;
        for (map<int, int>::const_iterator it = reduceCount.begin(); it != reduceCount.end(); ++it) {
            if (it->second > bestCount) {
                best = it->first;
                bestCount = it->second;
            }
        }
        if (best >= 0) {
            Action d;
            d.type = ActionType::REDUCE;
            d.value = best;
            defaults[s] = encode(d);
            ++defaultRows_;
        }
        for (map<string, Action>::const_iterator c = row->second.begin(); c != row->second.end(); ++c) {
            int v = encode(c->second);
            if (v == defaults[s]) continue;
            rows[s].push_back(make_pair(termIndex_[c->first], v));
        }
        // %nonassoc error entries stay explicit so the default reduction cannot fill them
        if (defaults[s] != 0) {
            set<pair<int, string> >::const_iterator e;
            for (e = t.errorCells.begin(); e != t.errorCells.end(); ++e)
                if (e->first == (int)s) rows[s].push_back(make_pair(termIndex_[e->second], 0));
            sort(rows[s].begin(), rows[s].end());
        }
    }
    vector<int> uniqueDefaults;
    vector<Row> unique = shareRows(rows, defaults, actionRowOf_, uniqueDefaults);
    actionDefault_ = uniqueDefaults;
    actionNext_.clear();
    actionCheck_.clear();
    actionBase_ = packRows(unique, actionNext_, actionCheck_);

    // GOTO: default target per non-terminal column, the rest packed by row
    vector<map<int, int> > colCount(nts_.size());
    for (map<int, map<string, int> >::const_iterator r = t.gotos.begin(); r != t.gotos.end(); ++r)
        for (map<string, int>::const_iterator c = r->second.begin(); c != r->second.end(); ++c)
            ++colCount[(size_t)ntIndex_[c->first]][c->second];
    gotoDefault_.assign(nts_.size(), -1);
    for (size_t k = 0; k < nts_.size(); ++k) {
        int bestCount = 0;
        for (map<int, int>::const_iterator it = colCount[k].begin(); it != colCount[k].end(); ++it) {
            if (it->second > bestCount) {
                gotoDefault_[k] = it->first;
                bestCount = it->second;
            }
        }
    }
    vector<Row> grows(states);
    for (map<int, map<string, int> >::const_iterator r = t.gotos.begin(); r != t.gotos.end(); ++r) {
        for (map<string, int>::const_iterator c = r->second.begin(); c != r->second.end(); ++c) {
            int col = ntIndex_[c->first];
            if (c->second != gotoDefault_[(size_t)col])
                grows[(size_t)r->first].push_back(make_pair(col, c->second));
        }
    }
    vector<int> noDefaults(states, 0);
    vector<int> ignored;
    vector<Row> gunique = shareRows(grows, noDefaults, gotoRowOf_, ignored);
    gotoNext_.clear();
    gotoCheck_.clear();
    gotoBase_ = packRows(gunique, gotoNext_, gotoCheck_);

    // Sizes in bytes with 16-bit cells
    rawBytes_ = 2L * (long)states * (long)(terms_.size() + nts_.size());
    packedBytes_ = 2L * (long)(actionRowOf_.size() + actionDefault_.size() + actionBase_.size() +
                               actionNext_.size() + actionCheck_.size() + gotoRowOf_.size() +
                               gotoDefault_.size() + gotoBase_.size() + gotoNext_.size() +
                               gotoCheck_.size());
}

bool PackedTable::action(int state, const string& symbol, Action& out) const {
    map<string, int>::const_iterator ti = termIndex_.find(symbol);
    int row = actionRowOf_[(size_t)state];
    int v = ti == termIndex_.end() ? INT_MIN
                                   : lookup(actionBase_, actionNext_, actionCheck_, row, ti->second);
    if (v == INT_MIN) v = actionDefault_[(size_t)row];
    if (v == 0) return false;
    out = decode(v);
    return true;
}

bool PackedTable::hasDefaultReduce(int state) const {
    return actionDefault_[(size_t)actionRowOf_[(size_t)state]] != 0;
}

int PackedTable::gotoState(int state, const string& nonterminal) const {
    map<string, int>::const_iterator ni = ntIndex_.find(nonterminal);
    if (ni == ntIndex_.end()) return -1;
    int v = lookup(gotoBase_, gotoNext_, gotoCheck_, gotoRowOf_[(size_t)state], ni->second);
    return v == INT_MIN ? gotoDefault_[(size_t)ni->second] : v;
}

vector<string> PackedTable::expected(int state) const {
    vector<string> out;
    int row = actionRowOf_[(size_t)state];
    for (size_t k = 0; k < terms_.size(); ++k) {
        int v = lookup(actionBase_, actionNext_, actionCheck_, row, (int)k);
        if (v != INT_MIN && v != 0) out.push_back(terms_[k]);
    }
    return out;
}

bool PackedTable::verify(const ParsingTable& t, ostream& os) const {
    int bad = 0;
    long checked = 0;
    for (size_t s = 0; s < t.stateItems.size(); ++s) {
        int def = actionDefault_[(size_t)actionRowOf_[s]];
        for (size_t k = 0; k < terms_.size(); ++k) {
            const Action* raw = t.findAction((int)s, terms_[k]);
            Action got;
            bool has = action((int)s, terms_[k], got);
            ++checked;
            bool ok;
            bool forcedError = t.errorCells.count(make_pair((int)s, terms_[k])) > 0;
            if (raw) ok = has && got == *raw;
            else if (forcedError) ok = !has;
            else ok = !has || (def != 0 && encode(got) == def);
            if (!ok && bad++ < 10)
                os << "COMPRESS DIFF state " << s << " on '" << terms_[k] << "'\n";
        }
        map<int, map<string, int> >::const_iterator row = t.gotos.find((int)s);
        if (row == t.gotos.end()) continue;
        for (map<string, int>::const_iterator c = row->second.begin(); c != row->second.end(); ++c) {
            ++checked;
            if (gotoState((int)s, c->first) != c->second && bad++ < 10)
                os << "COMPRESS DIFF GOTO state " << s << " on " << c->first << "\n";
        }
    }
    os << "VERIFY COMPRESS " << t.name << (bad == 0 ? " OK" : " FAILED") << " (" << checked
       << " cells checked)\n";
    return bad == 0;
}
