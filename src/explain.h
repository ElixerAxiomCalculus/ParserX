#ifndef EXPLAIN_H
#define EXPLAIN_H

#include <map>
#include <ostream>
#include <string>
#include <vector>

#include "comparison.h"
#include "first_follow.h"
#include "grammar.h"
#include "table.h"

// Shortest terminal strings derivable from each non-terminal
class ShortestYield {
public:
    explicit ShortestYield(const Grammar& g);
    bool productive(const std::string& symbol) const;
    std::vector<std::string> expand(const std::vector<std::string>& symbols) const;

private:
    const Grammar& g_;
    std::map<std::string, int> length_;
    std::map<std::string, int> choice_;
};

// The chain of reasons that put terminal a into FOLLOW(A)
std::string followChain(const Grammar& g, const FirstFollow& ff, const std::string& A,
                        const std::string& a, std::string* shortCause);

// Shortest prefix of terminals that drives the parser for t into state with
// symbol next; empty result with ok=false when no candidate could be verified
std::vector<std::string> findWitness(const Grammar& g, const ParsingTable& t,
                                     const LRMachine& m, int state, const std::string& symbol,
                                     bool& ok, int& tried);

// --explain: one section per parser, plus EXPLAIN summary lines
void explainConflicts(std::ostream& os, const Grammar& g, const FirstFollow& ff,
                      const BuiltTables& bt, std::vector<std::string>& summary);

#endif
