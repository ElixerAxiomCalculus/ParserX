#ifndef LEXGEN_H
#define LEXGEN_H

#include <ostream>
#include <set>
#include <string>
#include <vector>

#include "lexer.h"
#include "regex.h"

struct LexRule {
    std::string name;
    std::string pattern;
    bool skip = false;
    bool literal = false;
    int line = 0;
};

// Lexer generated from a .lex spec file:
//   NAME    REGEX        token rule (earlier rules win ties)
//   NAME                 literal token whose text is NAME
//   %skip   REGEX        text to discard (spaces, comments)
//   %literals A B ...    several literal tokens
//   %keywords A B ...    identifiers equal to one of these become that token
// Longest match first, then rule order.
class GeneratedLexer {
public:
    bool loadSpec(const std::string& path, std::string& error);
    bool loadSpecFromString(const std::string& text, std::string& error);
    LexResult tokenize(const std::string& source) const;

    int nfaStates() const { return nfaStates_; }
    int dfaStates() const { return dfaStates_; }
    int minDfaStates() const { return dfa_.size(); }
    const std::vector<LexRule>& rules() const { return rules_; }
    const std::set<std::string>& keywords() const { return keywords_; }
    void dumpDfa(std::ostream& os) const;

private:
    std::vector<LexRule> rules_;
    std::set<std::string> keywords_;
    Dfa dfa_;
    int nfaStates_ = 0;
    int dfaStates_ = 0;
};

#endif
