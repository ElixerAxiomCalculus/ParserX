#ifndef BENCH_H
#define BENCH_H

#include <functional>
#include <ostream>
#include <string>

#include "first_follow.h"
#include "grammar.h"
#include "lexer.h"

struct BenchOptions {
    int repeat = 5;
    long scaleLimitSeconds = 60;
};

// --bench: a warm-up build, then `repeat` timed builds of every table and
// timed parses of the input with every parser (median and minimum), then a
// scaling series on the input repeated to about 10^3 .. 10^6 tokens. The
// 10^6 step runs only when the 10^5 step suggests it takes under a minute.
void runBenchmark(const Grammar& g, const FirstFollow& ff, const std::string& source,
                  const std::function<LexResult(const std::string&)>& lex,
                  const BenchOptions& opt, std::ostream& os);

#endif
