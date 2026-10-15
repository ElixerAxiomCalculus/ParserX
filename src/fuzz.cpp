#include "fuzz.h"
#include "parser.h"
#include "util.h"

#include <algorithm>
#include <climits>
#include <fstream>
#include <iomanip>
#include <map>

using namespace std;

namespace {

const int kInf = INT_MAX / 4;
const size_t kLengthCap = 300;
const int kHistogramBuckets = 10;
const int kShowExamples = 10;

}

// Generator
SentenceGenerator::SentenceGenerator(const Grammar& g, unsigned seed, int maxDepth)
    : g_(g), rng_(seed), maxDepth_(maxDepth) {
    size_t n = g.symbols().size();
    height_.assign(n, kInf);
    length_.assign(n, kInf);
    for (size_t k = 0; k < g.terminals().size(); ++k) {
        int id = g.symbolId(g.terminals()[k]);
        height_[(size_t)id] = 0;
        length_[(size_t)id] = 1;
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t k = 0; k < g.productions().size(); ++k) {
            const Production& p = g.productions()[k];
            int h = 0;
            int len = 0;
            for (size_t i = 0; i < p.rhs.size() && h < kInf; ++i) {
                int id = g.symbolId(p.rhs[i]);
                h = max(h, height_[(size_t)id]);
                len = min(kInf, len + length_[(size_t)id]);
            }
            if (h >= kInf) continue;
            size_t lhs = (size_t)g.symbolId(p.lhs);
            if (h + 1 < height_[lhs]) {
                height_[lhs] = h + 1;
                changed = true;
            }
            if (len < length_[lhs]) {
                length_[lhs] = len;
                changed = true;
            }
        }
    }
}

unsigned SentenceGenerator::next(unsigned bound) {
    return bound == 0 ? 0 : (unsigned)(rng_() % bound);
}

int SentenceGenerator::minHeight(const string& symbol) const {
    int id = g_.symbolId(symbol);
    return id < 0 ? kInf : height_[(size_t)id];
}

int SentenceGenerator::minLength(const string& symbol) const {
    int id = g_.symbolId(symbol);
    return id < 0 ? kInf : length_[(size_t)id];
}

bool SentenceGenerator::canGenerate() const {
    return minHeight(g_.startSymbol()) < kInf;
}

int SentenceGenerator::pickProduction(const string& lhs, bool shortest) {
    const vector<int>& prods = g_.productionsOf(lhs);
    vector<int> usable;
    int best = -1;
    int bestH = kInf;
    int bestLen = kInf;
    for (size_t k = 0; k < prods.size(); ++k) {
        const Production& p = g_.production(prods[k]);
        int h = 0;
        int len = 0;
        for (size_t i = 0; i < p.rhs.size(); ++i) {
            h = max(h, minHeight(p.rhs[i]));
            len = min(kInf, len + minLength(p.rhs[i]));
        }
        if (h >= kInf) continue;
        usable.push_back(prods[k]);
        if (h < bestH || (h == bestH && len < bestLen)) {
            best = prods[k];
            bestH = h;
            bestLen = len;
        }
    }
    if (shortest || usable.empty()) return best;
    return usable[next((unsigned)usable.size())];
}

vector<string> SentenceGenerator::sentence() {
    vector<string> out;
    vector<pair<string, int> > work(1, make_pair(g_.startSymbol(), 0));
    while (!work.empty()) {
        pair<string, int> cur = work.back();
        work.pop_back();
        if (!g_.isNonterminal(cur.first)) {
            out.push_back(cur.first);
            continue;
        }
        bool shortest = cur.second >= maxDepth_ || out.size() > kLengthCap;
        int p = pickProduction(cur.first, shortest);
        if (p < 0) continue;
        const vector<string>& rhs = g_.production(p).rhs;
        for (size_t k = rhs.size(); k > 0; --k) work.push_back(make_pair(rhs[k - 1], cur.second + 1));
    }
    return out;
}

// Plausible lexemes for printing
vector<Token> SentenceGenerator::toTokens(const vector<string>& terminals) {
    static const char* const kIds[] = {"a", "b", "c", "x", "y"};
    static const char* const kRelops[] = {"<", ">", "==", "!="};
    vector<Token> tokens;
    for (size_t k = 0; k < terminals.size(); ++k) {
        Token t;
        t.type = terminals[k];
        if (t.type == "id") t.lexeme = kIds[next(5)];
        else if (t.type == "num") t.lexeme = to_string(1 + next(9));
        else if (t.type == "real") t.lexeme = to_string(1 + next(9)) + ".5";
        else if (t.type == "relop") t.lexeme = kRelops[next(4)];
        else t.lexeme = t.type;
        t.line = 1;
        t.column = (int)k + 1;
        tokens.push_back(t);
    }
    Token end;
    end.type = "$";
    end.lexeme = "$";
    end.line = 1;
    end.column = (int)terminals.size() + 1;
    tokens.push_back(end);
    return tokens;
}

namespace {

struct Outcome {
    bool accepted = false;
    int steps = 0;
    int errorIndex = -1;
    int delay = 0;
    int expectedSize = 0;
    int conflictMoves = 0;
    bool mergedConflict = false;
    int recoverErrors = 0;
};

struct Input {
    string kind;
    vector<string> terminals;
    bool clrValid = false;
    vector<Outcome> out;
};

struct Stats {
    int accepted = 0;
    int validRejected = 0;
    vector<int> delays;
    long expectedSum = 0;
    long conflictMoves = 0;
    vector<int> histogram;
};

string fixed3(double v) { return utilFixed3(v); }

double mean(const vector<int>& v) {
    if (v.empty()) return 0.0;
    double s = 0;
    for (size_t k = 0; k < v.size(); ++k) s += v[k];
    return s / (double)v.size();
}

double median(vector<int> v) {
    if (v.empty()) return 0.0;
    sort(v.begin(), v.end());
    size_t m = v.size() / 2;
    return v.size() % 2 ? v[m] : (v[m - 1] + v[m]) / 2.0;
}

string csvQuote(const string& s) {
    string out = "\"";
    for (size_t k = 0; k < s.size(); ++k) {
        if (s[k] == '"') out += "\"\"";
        else out += s[k];
    }
    return out + "\"";
}

// One mutant with exactly one edit
vector<string> mutate(SentenceGenerator& gen, const vector<string>& base, const vector<string>& terms,
                      int kind) {
    vector<string> m = base;
    if (kind == 0 && !m.empty()) {
        m.erase(m.begin() + gen.next((unsigned)m.size()));
    } else if (kind == 1) {
        m.insert(m.begin() + gen.next((unsigned)m.size() + 1), terms[gen.next((unsigned)terms.size())]);
    } else if (kind == 2 && !m.empty()) {
        size_t at = gen.next((unsigned)m.size());
        m[at] = terms[gen.next((unsigned)terms.size())];
    } else if (kind == 3 && m.size() >= 2) {
        size_t at = gen.next((unsigned)m.size() - 1);
        swap(m[at], m[at + 1]);
    }
    return m;
}

Outcome runOne(const Grammar& g, const ParsingTable& t, const PackedTable* packed,
               const vector<Token>& tokens, bool recover) {
    Parser p(g, t, packed);
    ParseResult r = p.parse(tokens, false);
    Outcome o;
    o.accepted = r.accepted;
    o.steps = r.stepCount;
    o.conflictMoves = r.conflictMoves;
    for (set<int>::const_iterator it = r.conflictStates.begin(); it != r.conflictStates.end(); ++it)
        if (t.stateNotes[(size_t)*it].compare(0, 6, "merged") == 0) o.mergedConflict = true;
    if (!r.accepted) {
        o.errorIndex = r.error.tokenIndex;
        o.delay = r.reductionsBeforeError;
        o.expectedSize = (int)r.error.expected.size();
    }
    if (recover) {
        ParseOptions ro;
        ro.recordTrace = false;
        ro.recover = true;
        o.recoverErrors = (int)p.parse(tokens, ro).errors.size();
    }
    return o;
}

}

bool runFuzz(const Grammar& g, const BuiltTables& bt, const FuzzOptions& opt, ostream& os) {
    const ParsingTable* tables[4] = {&bt.slr, &bt.lalr, &bt.clr, &bt.pgm};
    const size_t kParsers = 4;
    const size_t kClr = 2;
    SentenceGenerator gen(g, opt.seed, opt.maxDepth);
    vector<string> terms;
    for (size_t k = 0; k < g.terminals().size(); ++k)
        if (g.terminals()[k] != "$" && g.terminals()[k] != "error") terms.push_back(g.terminals()[k]);

    os << "Generator: seed " << opt.seed << ", max depth " << opt.maxDepth << ", " << opt.count
       << " sentence(s), 4 one-edit mutants each (delete, insert, replace, swap)"
       << (opt.compressed ? ", compressed tables" : "") << "\n";
    if (!gen.canGenerate()) os << "The start symbol derives no terminal string; nothing generated.\n";

    // Generate inputs
    static const char* const kKinds[] = {"delete", "insert", "replace", "swap"};
    vector<Input> inputs;
    for (int n = 0; n < opt.count && gen.canGenerate(); ++n) {
        Input in;
        in.kind = "valid";
        in.terminals = gen.sentence();
        inputs.push_back(in);
        for (int k = 0; k < 4 && !terms.empty(); ++k) {
            Input m;
            m.kind = kKinds[k];
            m.terminals = mutate(gen, in.terminals, terms, k);
            inputs.push_back(m);
        }
    }

    // Run every parser
    for (size_t i = 0; i < inputs.size(); ++i) {
        vector<Token> tokens = gen.toTokens(inputs[i].terminals);
        inputs[i].clrValid = Parser(g, bt.clr).parse(tokens, false).accepted;
        for (size_t k = 0; k < kParsers; ++k) {
            const PackedTable* packed = opt.compressed ? &packedFor(bt, tables[k]->name) : 0;
            inputs[i].out.push_back(runOne(g, *tables[k], packed, tokens, opt.recover));
        }
    }

    // Statistics
    vector<Stats> stats(kParsers);
    vector<bool> sound(kParsers);
    for (size_t k = 0; k < kParsers; ++k) {
        stats[k].histogram.assign(kHistogramBuckets + 1, 0);
        sound[k] = tables[k]->rrConflicts() == 0 && tables[k]->nonassocErrors == 0;
    }
    int valid = 0;
    int agree = 0;
    int disagreements = 0;
    int explained = 0;
    int lalrDisagreements = 0;
    int lalrMerged = 0;
    vector<string> failures;
    vector<string> examples;
    for (size_t i = 0; i < inputs.size(); ++i) {
        const Input& in = inputs[i];
        if (in.clrValid) ++valid;
        bool same = true;
        for (size_t k = 0; k < kParsers; ++k) {
            const Outcome& o = in.out[k];
            if (o.accepted) ++stats[k].accepted;
            if (in.kind == "valid" && !o.accepted) ++stats[k].validRejected;
            if (!in.clrValid && !o.accepted) {
                stats[k].delays.push_back(o.delay);
                stats[k].expectedSum += o.expectedSize;
                ++stats[k].histogram[(size_t)min(o.delay, kHistogramBuckets)];
            }
            stats[k].conflictMoves += o.conflictMoves;
            if (o.accepted != in.out[0].accepted) same = false;
        }
        if (same) {
            ++agree;
        } else {
            ++disagreements;
            bool why = false;
            for (size_t k = 0; k < kParsers; ++k)
                if (in.out[k].accepted != in.clrValid && in.out[k].conflictMoves > 0) why = true;
            if (why) ++explained;
            if (in.out[1].accepted != in.clrValid) {
                ++lalrDisagreements;
                if (in.out[1].mergedConflict) ++lalrMerged;
            }
            if ((int)examples.size() < kShowExamples) {
                string ex = "  [" + in.kind + "] " + utilJoin(in.terminals, " ") + "  ->";
                for (size_t k = 0; k < kParsers; ++k)
                    ex += " " + tables[k]->name + "=" + (in.out[k].accepted ? "acc" : "rej");
                examples.push_back(ex);
            }
        }
        // Invariants among sound parsers
        for (size_t k = 0; k < kParsers; ++k) {
            if (!sound[k]) continue;
            const Outcome& o = in.out[k];
            string where = " on input #" + to_string(i) + " [" + in.kind + "] " +
                           utilJoin(in.terminals, " ");
            if (in.kind == "valid" && !o.accepted)
                failures.push_back(tables[k]->name + " rejected a generated sentence" + where);
            if (sound[kClr] && o.accepted != in.out[kClr].accepted)
                failures.push_back(tables[k]->name + " and CLR disagree on acceptance" + where);
            if (sound[kClr] && !o.accepted && !in.out[kClr].accepted) {
                if (o.errorIndex != in.out[kClr].errorIndex)
                    failures.push_back(tables[k]->name + " detected the error at token " +
                                       to_string(o.errorIndex) + ", CLR at " +
                                       to_string(in.out[kClr].errorIndex) + where);
                if (!opt.compressed && in.out[kClr].delay > o.delay)
                    failures.push_back("CLR reduced more often before the error than " +
                                       tables[k]->name + where);
            }
        }
    }

    // Report
    int total = (int)inputs.size();
    int generated = 0;
    for (size_t i = 0; i < inputs.size(); ++i)
        if (inputs[i].kind == "valid") ++generated;
    os << "Inputs: " << total << " (" << generated << " generated sentences + "
       << (total - generated) << " mutants); reference CLR accepts " << valid << ", rejects "
       << (total - valid) << "\n\n";
    ios::fmtflags saved = os.flags();
    os << left << setw(8) << "Parser" << setw(10) << "Accepted" << setw(10) << "ValidRej"
       << setw(11) << "MeanDelay" << setw(13) << "MedianDelay" << setw(10) << "MaxDelay"
       << setw(14) << "MeanExpected" << "ConflictMoves\n";
    os << utilRepeat('-', 90) << "\n";
    for (size_t k = 0; k < kParsers; ++k) {
        const Stats& s = stats[k];
        int maxDelay = s.delays.empty() ? 0 : *max_element(s.delays.begin(), s.delays.end());
        double meanExp = s.delays.empty() ? 0.0 : (double)s.expectedSum / (double)s.delays.size();
        os << left << setw(8) << tables[k]->name << setw(10) << s.accepted << setw(10)
           << s.validRejected << setw(11) << fixed3(mean(s.delays)) << setw(13)
           << fixed3(median(s.delays)) << setw(10) << maxDelay << setw(14) << fixed3(meanExp)
           << s.conflictMoves << "\n";
    }
    os << "\nDetection delay histogram (reductions on the error token, CLR-invalid inputs):\n";
    os << left << setw(8) << "Delay";
    for (size_t k = 0; k < kParsers; ++k) os << setw(8) << tables[k]->name;
    os << "\n";
    for (int b = 0; b <= kHistogramBuckets; ++b) {
        os << left << setw(8) << (b == kHistogramBuckets ? to_string(b) + "+" : to_string(b));
        for (size_t k = 0; k < kParsers; ++k) os << setw(8) << stats[k].histogram[(size_t)b];
        os << "\n";
    }
    os.flags(saved);
    os << "\nAgreement on acceptance: " << agree << "/" << total << " inputs ("
       << fixed3(total ? 100.0 * agree / total : 100.0) << "%)\n";
    os << "Disagreements: " << disagreements << " (explained by a conflict-resolved cell: "
       << explained << "; LALR ones in merged states: " << lalrMerged << "/" << lalrDisagreements
       << ")\n";
    for (size_t k = 0; k < examples.size(); ++k) os << examples[k] << "\n";

    os << "\nInvariants (checked for parsers without reduce/reduce conflicts or %nonassoc errors:";
    bool anySound = false;
    for (size_t k = 0; k < kParsers; ++k) {
        if (!sound[k]) continue;
        os << " " << tables[k]->name;
        anySound = true;
    }
    os << (anySound ? "" : " none") << ")\n";
    if (explained != disagreements)
        failures.push_back(to_string(disagreements - explained) +
                           " disagreement(s) not explained by a conflict-resolved cell");
    for (size_t k = 0; k < failures.size() && k < 20; ++k) os << "  FAIL " << failures[k] << "\n";

    // CSV
    if (!opt.csvPath.empty()) {
        ofstream csv(opt.csvPath.c_str(), ios::binary);
        csv << "id,kind,length,clr_valid";
        for (size_t k = 0; k < kParsers; ++k) {
            string n = tables[k]->name;
            csv << "," << n << "_accepted," << n << "_steps," << n << "_error_index," << n
                << "_delay," << n << "_expected";
            if (opt.recover) csv << "," << n << "_recover_errors";
        }
        csv << ",tokens\n";
        for (size_t i = 0; i < inputs.size(); ++i) {
            const Input& in = inputs[i];
            csv << i << "," << in.kind << "," << in.terminals.size() << "," << (in.clrValid ? 1 : 0);
            for (size_t k = 0; k < kParsers; ++k) {
                const Outcome& o = in.out[k];
                csv << "," << (o.accepted ? 1 : 0) << "," << o.steps << "," << o.errorIndex << ","
                    << o.delay << "," << o.expectedSize;
                if (opt.recover) csv << "," << o.recoverErrors;
            }
            csv << "," << csvQuote(utilJoin(in.terminals, " ")) << "\n";
        }
        os << "CSV written to " << opt.csvPath << " (" << inputs.size() << " rows)\n";
    }

    // Machine-readable lines
    os << "\n" << utilRepeat('-', 70) << "\n";
    for (size_t k = 0; k < kParsers; ++k) {
        const Stats& s = stats[k];
        int maxDelay = s.delays.empty() ? 0 : *max_element(s.delays.begin(), s.delays.end());
        double meanExp = s.delays.empty() ? 0.0 : (double)s.expectedSum / (double)s.delays.size();
        os << "FUZZ " << tables[k]->name << " inputs=" << total << " accepted=" << s.accepted
           << " mean_delay=" << fixed3(mean(s.delays)) << " max_delay=" << maxDelay
           << " mean_expected=" << fixed3(meanExp) << " median_delay=" << fixed3(median(s.delays))
           << " valid_rejected=" << s.validRejected << "\n";
    }
    os << "FUZZ AGREEMENT inputs=" << total << " agree=" << agree << " disagreements=" << disagreements
       << " explained=" << explained << " lalr_merged=" << lalrMerged << "/" << lalrDisagreements << "\n";
    os << "FUZZ INVARIANTS " << (failures.empty() ? "OK" : "FAILED (" + to_string(failures.size()) + ")")
       << "\n";
    return failures.empty();
}
