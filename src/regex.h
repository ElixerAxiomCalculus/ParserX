#ifndef REGEX_H
#define REGEX_H

#include <array>
#include <bitset>
#include <ostream>
#include <string>
#include <vector>

// Thompson NFA: every state has epsilon edges and at most one character edge.
struct NfaState {
    std::vector<int> eps;
    std::bitset<256> chars;
    int next = -1;
    int accept = -1;
};

struct Nfa {
    std::vector<NfaState> states;
    int start = -1;
};

struct Dfa {
    int start = 0;
    std::vector<std::array<int, 256> > next;
    std::vector<int> accept;

    int size() const { return (int)next.size(); }
};

// Regex syntax: concatenation, |, *, +, ?, ( ), [a-z] [^...], ., and escapes
// \t \n \r \s \d \w and \c for any other character c.
// Adds a fragment for the pattern to nfa; returns its start state or -1 with an error.
int regexToNfa(const std::string& pattern, int rule, Nfa& nfa, std::string& error);
int literalToNfa(const std::string& text, int rule, Nfa& nfa);
bool nfaFragmentMatchesEmpty(const Nfa& nfa, int start);

// Subset construction; the accepting rule of a DFA state is the lowest rule index.
Dfa nfaToDfa(const Nfa& nfa);
// Moore partition refinement, keeping each state's accepting rule.
Dfa minimizeDfa(const Dfa& dfa);

void printDfa(std::ostream& os, const Dfa& dfa, const std::vector<std::string>& ruleNames);

#endif
