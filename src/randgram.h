#ifndef RANDGRAM_H
#define RANDGRAM_H

#include <ostream>
#include <string>

struct RandomGrammarOptions {
    int count = 0;
    unsigned seed = 20261015u;
    int maxNonterminals = 4;
    int maxTerminals = 4;
    int maxAlternatives = 3;
    int maxRhsLength = 3;
    std::string csvPath;
};

// Empirical study: generate reduced random grammars (every non-terminal
// reachable and productive), classify each and summarise the classes and
// table sizes. Returns the number of grammars studied.
int runRandomGrammarStudy(const RandomGrammarOptions& opt, std::ostream& os);

#endif
