#include "randgram.h"
#include "classify.h"
#include "comparison.h"
#include "first_follow.h"
#include "grammar.h"
#include "util.h"

#include <fstream>
#include <iomanip>
#include <map>
#include <random>
#include <vector>

using namespace std;

namespace {

const int kMaxAttempts = 200;

unsigned pick(mt19937& rng, unsigned bound) { return bound == 0 ? 0 : (unsigned)(rng() % bound); }

// One random grammar as grammar-file text
string randomGrammarText(mt19937& rng, const RandomGrammarOptions& opt) {
    int nts = 2 + (int)pick(rng, (unsigned)max(1, opt.maxNonterminals - 1));
    int ts = 2 + (int)pick(rng, (unsigned)max(1, opt.maxTerminals - 1));
    string text;
    for (int a = 0; a < nts; ++a) {
        int alts = 1 + (int)pick(rng, (unsigned)opt.maxAlternatives);
        text += string(1, (char)('A' + a)) + " ->";
        for (int k = 0; k < alts; ++k) {
            if (k) text += " |";
            int len = (int)pick(rng, (unsigned)opt.maxRhsLength + 1);
            if (len == 0) text += " eps";
            for (int s = 0; s < len; ++s) {
                bool nonterminal = pick(rng, 3) == 0;
                if (nonterminal) text += string(" ") + (char)('A' + pick(rng, (unsigned)nts));
                else text += string(" ") + (char)('a' + pick(rng, (unsigned)ts));
            }
        }
        text += "\n";
    }
    return text;
}

string oneLine(const string& text) {
    string out;
    for (size_t k = 0; k < text.size(); ++k) out += text[k] == '\n' ? string("; ") : string(1, text[k]);
    return out;
}

}

int runRandomGrammarStudy(const RandomGrammarOptions& opt, ostream& os) {
    mt19937 rng(opt.seed);
    os << "Parameters: seed " << opt.seed << ", " << opt.count << " grammar(s), 2-"
       << opt.maxNonterminals << " non-terminals, 2-" << opt.maxTerminals << " terminals, 1-"
       << opt.maxAlternatives << " alternatives per non-terminal, right sides of 0-"
       << opt.maxRhsLength << " symbols (eps for 0), a symbol is a non-terminal with probability 1/3;\n"
       << "grammars with an unproductive or unreachable non-terminal or a duplicate production "
          "are discarded and redrawn\n";

    ofstream csv;
    if (!opt.csvPath.empty()) {
        csv.open(opt.csvPath.c_str(), ios::binary);
        csv << "id,class,nonterminals,terminals,productions,raw_lr0,raw_slr,raw_lalr,raw_clr,"
               "slr_states,lalr_states,clr_states,pgm_states,slr_entries,lalr_entries,clr_entries,"
               "pgm_entries,grammar\n";
    }
    static const char* const kClasses[] = {"LR(0)", "SLR(1)", "LALR(1)", "LR(1)", "not LR(1)", 0};
    map<string, int> perClass;
    double ratioSum = 0;
    double pgmRatioSum = 0;
    long entries[4] = {0, 0, 0, 0};
    int studied = 0;
    int discarded = 0;
    for (int n = 0; n < opt.count; ++n) {
        Grammar g;
        string text;
        bool ok = false;
        for (int attempt = 0; attempt < kMaxAttempts && !ok; ++attempt) {
            text = randomGrammarText(rng, opt);
            string err;
            ok = g.loadFromString(text, "random", ".", err) && g.warnings().empty();
            if (!ok) ++discarded;
        }
        if (!ok) continue;
        FirstFollow ff(g);
        BuiltTables bt = buildAllTables(g, ff);
        GrammarClass c = classifyGrammar(g, ff, bt);
        ++perClass[c.name];
        ++studied;
        ratioSum += (double)bt.clr.stateItems.size() / (double)bt.lalr.stateItems.size();
        pgmRatioSum += (double)bt.pgm.stateItems.size() / (double)bt.lalr.stateItems.size();
        entries[0] += bt.slr.totalEntries();
        entries[1] += bt.lalr.totalEntries();
        entries[2] += bt.clr.totalEntries();
        entries[3] += bt.pgm.totalEntries();
        if (csv.is_open()) {
            csv << n << "," << c.name << "," << (g.nonterminals().size() - 1) << ","
                << (g.terminals().size() - 1) << "," << (g.productions().size() - 1) << "," << c.lr0
                << "," << c.slr << "," << c.lalr << "," << c.clr << "," << bt.slr.stateItems.size()
                << "," << bt.lalr.stateItems.size() << "," << bt.clr.stateItems.size() << ","
                << bt.pgm.stateItems.size() << "," << bt.slr.totalEntries() << ","
                << bt.lalr.totalEntries() << "," << bt.clr.totalEntries() << ","
                << bt.pgm.totalEntries() << ",\"" << oneLine(text) << "\"\n";
        }
    }
    double d = studied ? (double)studied : 1.0;
    os << "Studied " << studied << " grammar(s) (" << discarded << " draws discarded as not reduced)\n\n";
    os << left << setw(12) << "Class" << setw(8) << "Count" << "Fraction\n";
    for (size_t k = 0; kClasses[k]; ++k) {
        int cnt = perClass[kClasses[k]];
        os << left << setw(12) << kClasses[k] << setw(8) << cnt << utilFixed3(cnt / d) << "\n";
    }
    os << "\nMean CLR/LALR state ratio: " << utilFixed3(ratioSum / d)
       << "; mean PGM/LALR state ratio: " << utilFixed3(pgmRatioSum / d) << "\n";
    os << "Mean ACTION+GOTO entries: SLR " << utilFixed3(entries[0] / d) << ", LALR "
       << utilFixed3(entries[1] / d) << ", CLR " << utilFixed3(entries[2] / d) << ", PGM "
       << utilFixed3(entries[3] / d) << "\n";
    if (csv.is_open()) os << "CSV written to " << opt.csvPath << "\n";

    os << "\n" << utilRepeat('-', 70) << "\n";
    os << "RANDOM grammars=" << studied << " seed=" << opt.seed;
    for (size_t k = 0; kClasses[k]; ++k) {
        string key = kClasses[k];
        for (size_t c = 0; c < key.size(); ++c)
            if (key[c] == ' ') key[c] = '-';
        os << " " << key << "=" << perClass[kClasses[k]];
    }
    os << "\n";
    os << "RANDOM mean_clr_lalr_state_ratio=" << utilFixed3(ratioSum / d)
       << " mean_pgm_lalr_state_ratio=" << utilFixed3(pgmRatioSum / d)
       << " mean_entries_slr=" << utilFixed3(entries[0] / d) << " mean_entries_lalr="
       << utilFixed3(entries[1] / d) << " mean_entries_clr=" << utilFixed3(entries[2] / d)
       << " mean_entries_pgm=" << utilFixed3(entries[3] / d) << "\n";
    return studied;
}
