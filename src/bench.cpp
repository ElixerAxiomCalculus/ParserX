#include "bench.h"
#include "clr.h"
#include "comparison.h"
#include "lalr.h"
#include "lalr_dp.h"
#include "parser.h"
#include "pgm.h"
#include "slr.h"
#include "util.h"

#include <algorithm>
#include <chrono>
#include <iomanip>

using namespace std;

namespace {

typedef chrono::steady_clock Clock;

double microsSince(Clock::time_point start) {
    return chrono::duration<double, micro>(Clock::now() - start).count();
}

double median(vector<double> v) {
    sort(v.begin(), v.end());
    size_t m = v.size() / 2;
    return v.size() % 2 ? v[m] : (v[m - 1] + v[m]) / 2.0;
}

double minimum(const vector<double>& v) { return *min_element(v.begin(), v.end()); }

// One timed construction of each table
void timeBuilds(const Grammar& g, const FirstFollow& ff, vector<vector<double> >& times) {
    Clock::time_point t0 = Clock::now();
    LRMachine lr0 = buildSLRMachine(g);
    buildParsingTable(g, ff, lr0);
    times[0].push_back(microsSince(t0));

    t0 = Clock::now();
    LRMachine clr = buildCLRMachine(g, ff);
    buildParsingTable(g, ff, buildLALRMachine(g, clr));
    times[1].push_back(microsSince(t0));

    t0 = Clock::now();
    LRMachine lr0b = buildSLRMachine(g);
    buildParsingTable(g, ff, buildLALRMachineDP(g, ff, lr0b, 0));
    times[2].push_back(microsSince(t0));

    t0 = Clock::now();
    buildParsingTable(g, ff, buildCLRMachine(g, ff));
    times[3].push_back(microsSince(t0));

    t0 = Clock::now();
    buildParsingTable(g, ff, buildPGMMachine(g, ff));
    times[4].push_back(microsSince(t0));
}

string us(double v) { return utilFixed3(v); }

}

void runBenchmark(const Grammar& g, const FirstFollow& ff, const string& source,
                  const function<LexResult(const string&)>& lex, const BenchOptions& opt,
                  ostream& os) {
    static const char* const kBuilds[] = {"SLR", "LALR-merge", "LALR-dp", "CLR", "PGM"};
    const size_t kBuildCount = 5;
    vector<vector<double> > builds(kBuildCount);
    timeBuilds(g, ff, builds);
    for (size_t k = 0; k < kBuildCount; ++k) builds[k].clear();
    for (int r = 0; r < opt.repeat; ++r) timeBuilds(g, ff, builds);

    os << "Repetitions: " << opt.repeat << " (after one warm-up run); times in microseconds\n\n";
    os << left << setw(12) << "Build" << setw(14) << "median us" << "min us\n";
    for (size_t k = 0; k < kBuildCount; ++k)
        os << left << setw(12) << kBuilds[k] << setw(14) << us(median(builds[k])) << us(minimum(builds[k]))
           << "\n";

    BuiltTables bt = buildAllTables(g, ff);
    const ParsingTable* tables[4] = {&bt.slr, &bt.lalr, &bt.clr, &bt.pgm};
    LexResult base = lex(source);
    size_t tokens = base.tokens.size();
    os << "\n" << left << setw(12) << "Parse" << setw(14) << "median us" << setw(14) << "min us"
       << "tokens/s (median)   input: " << tokens << " tokens\n";
    vector<string> lines;
    for (size_t k = 0; k < 4; ++k) {
        Parser p(g, *tables[k]);
        p.parse(base.tokens, false);
        vector<double> t;
        for (int r = 0; r < opt.repeat; ++r) {
            Clock::time_point t0 = Clock::now();
            p.parse(base.tokens, false);
            t.push_back(microsSince(t0));
        }
        double med = median(t);
        double rate = med > 0 ? tokens / (med / 1e6) : 0;
        os << left << setw(12) << tables[k]->name << setw(14) << us(med) << setw(14) << us(minimum(t))
           << us(rate) << "\n";
        lines.push_back("BENCH parse " + tables[k]->name + " median_us=" + us(med) + " min_us=" +
                        us(minimum(t)) + " tokens_per_s=" + us(rate));
    }

    // Scaling series on the repeated input
    os << "\nScaling (input repeated; one timed parse per size):\n";
    bool accepted = !base.errors.empty() ? false : Parser(g, bt.lalr).parse(base.tokens, false).accepted;
    Parser twice(g, bt.lalr);
    LexResult doubled = lex(source + "\n" + source);
    if (!accepted || tokens < 2 || !doubled.errors.empty() || !twice.parse(doubled.tokens, false).accepted) {
        os << "  skipped: the input repeated twice is not a valid program\n";
    } else {
        os << left << setw(10) << "tokens" << setw(12) << "lex us";
        for (size_t k = 0; k < 4; ++k) os << setw(12) << (tables[k]->name + " us");
        os << "tokens/s (LALR)\n";
        double lastLalr = 0;
        for (long target = 1000; target <= 1000000; target *= 10) {
            if (target == 1000000 && lastLalr * 10.0 > opt.scaleLimitSeconds * 1e6) {
                os << "  10^6 skipped: projected time over " << opt.scaleLimitSeconds << " s\n";
                break;
            }
            size_t copies = (size_t)max(1L, target / (long)(tokens - 1));
            string big;
            big.reserve(source.size() * copies + copies);
            for (size_t c = 0; c < copies; ++c) big += source + "\n";
            Clock::time_point t0 = Clock::now();
            LexResult lr = lex(big);
            double lexUs = microsSince(t0);
            os << left << setw(10) << lr.tokens.size() << setw(12) << us(lexUs);
            string line = "BENCH scale tokens=" + to_string(lr.tokens.size()) + " lex_us=" + us(lexUs);
            for (size_t k = 0; k < 4; ++k) {
                Parser p(g, *tables[k]);
                t0 = Clock::now();
                p.parse(lr.tokens, false);
                double t = microsSince(t0);
                if (k == 1) lastLalr = t;
                os << setw(12) << us(t);
                line += " " + tables[k]->name + "_us=" + us(t);
            }
            os << us(lastLalr > 0 ? lr.tokens.size() / (lastLalr / 1e6) : 0) << "\n";
            lines.push_back(line);
        }
    }
    os << "\n" << utilRepeat('-', 70) << "\n";
    for (size_t k = 0; k < kBuildCount; ++k)
        os << "BENCH build " << kBuilds[k] << " median_us=" << us(median(builds[k]))
           << " min_us=" << us(minimum(builds[k])) << "\n";
    for (size_t k = 0; k < lines.size(); ++k) os << lines[k] << "\n";
}
