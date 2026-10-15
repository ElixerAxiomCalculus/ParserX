#ifndef PARSER_H
#define PARSER_H

#include <ostream>
#include <set>
#include <string>
#include <vector>

#include "grammar.h"
#include "lexer.h"
#include "table.h"

class PackedTable;

struct ParseStep {
    int number = 0;
    std::vector<int> stateStack;
    std::vector<std::string> symbolStack;
    std::vector<Token> remaining;
    std::string action;
    int reduceProd = -1;
};

struct ParseError {
    bool hasError = false;
    std::string parserName;
    int state = 0;
    std::string unexpectedType;
    std::string unexpectedLexeme;
    int line = 0;
    int column = 0;
    int tokenIndex = -1;
    std::vector<std::string> expected;
    std::string reason;
    std::string context;
    std::string previousLexeme;
    std::vector<int> stateStack;
    std::vector<std::string> symbolStack;
};

// Concrete parse tree stored in an arena (no recursion needed to build or free it)
struct TreeNode {
    std::string symbol;
    int prod = -1;
    int token = -1;
    std::vector<int> kids;
};

struct ParseTree {
    std::vector<TreeNode> nodes;
    int root = -1;
};

struct ParseOptions {
    bool recordTrace = true;
    bool buildTree = false;
    bool recover = false;
    int maxErrors = 50;
};

struct ParseResult {
    bool accepted = false;
    std::vector<ParseStep> steps;
    int stepCount = 0;
    int errorStep = -1;
    int reductionsBeforeError = 0;
    int conflictMoves = 0;
    std::set<int> conflictStates;
    ParseError error;
    ParseTree tree;
    // With recovery: every reported error, tokens discarded, why it stopped
    std::vector<ParseError> errors;
    int skippedTokens = 0;
    std::string recoveryMode;
    std::string abortReason;
};

class Parser {
public:
    Parser(const Grammar& g, const ParsingTable& table, const PackedTable* packed = 0);
    ParseResult parse(const std::vector<Token>& tokens, bool recordTrace = true) const;
    ParseResult parse(const std::vector<Token>& tokens, const ParseOptions& opt) const;
    const ParsingTable& table() const { return table_; }

private:
    bool action(int state, const std::string& symbol, Action& out) const;
    int gotoState(int state, const std::string& nonterminal) const;
    std::vector<std::string> expected(int state) const;
    ParseError makeError(const std::vector<int>& states, const std::vector<std::string>& symbols,
                         const std::vector<Token>& tokens, size_t pos) const;
    std::string recoverSymbol() const;

    const Grammar& g_;
    const ParsingTable& table_;
    const PackedTable* packed_;
};

// The compact bracket form used by the TREE lines
std::string treeToCompactString(const ParseTree& tree, const std::vector<Token>& tokens);

void printTrace(std::ostream& os, const Grammar& g, const ParseResult& r,
                bool full);
void printParseError(std::ostream& os, const std::string& source,
                     const ParseResult& r);
void printRecoveredErrors(std::ostream& os, const ParseResult& r);

#endif
