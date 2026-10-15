#ifndef VISUAL_H
#define VISUAL_H

#include <string>
#include <vector>

#include "comparison.h"
#include "grammar.h"
#include "lexer.h"
#include "table.h"

// Graphviz DOT of one LR automaton; conflict states are drawn in red
bool writeAutomatonDot(const std::string& path, const Grammar& g, const LRMachine& m,
                       const ParsingTable& t, std::string& error);

// The CLR automaton with every state filled with the colour of the LALR state
// it is merged into; CLR states of LALR conflict states get a red border
bool writeLalrMergeDot(const std::string& path, const Grammar& g, const BuiltTables& bt,
                       std::string& error);

// One self-contained HTML page (inline CSS and JavaScript) stepping through
// the parse traces of several parsers side by side
bool writeHtmlTrace(const std::string& path, const Grammar& g,
                    const std::vector<const ParsingTable*>& tables, const std::vector<Token>& tokens,
                    const std::string& source, std::string& error);

// ACTION and GOTO tables as DIR/<parser>_action.csv and DIR/<parser>_goto.csv
bool writeCsvTables(const std::string& dir, const Grammar& g,
                    const std::vector<const ParsingTable*>& tables, std::vector<std::string>& written,
                    std::string& error);

// FILE.dot -> FILE_slr.dot
std::string withSuffix(const std::string& path, const std::string& suffix);

#endif
