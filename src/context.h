#ifndef CONTEXT_H
#define CONTEXT_H

#include <string>
#include <vector>

#include "grammar.h"
#include "parser.h"
#include "table.h"

// A sentence describing where a syntax error happened, built from the kernel
// items of the states on the parser stack and the %name directives, e.g.
//   in an assignment "l-value = expression", after an expression:
//   expected ';' or an operator (% * + - /)
std::string describeErrorContext(const Grammar& g, const ParsingTable& t,
                                 const ParseError& e);

// "a statement", "an expression"
std::string withArticle(const std::string& noun);

// Expected tokens as words: operators grouped, $ as end of input
std::string describeExpected(const std::vector<std::string>& expected);

#endif
