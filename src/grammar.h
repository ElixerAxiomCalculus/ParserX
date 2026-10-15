#ifndef GRAMMAR_H
#define GRAMMAR_H

#include <map>
#include <ostream>
#include <set>
#include <string>
#include <vector>

enum class Assoc { NONE, LEFT, RIGHT, NONASSOC };

struct Production {
    int id = -1;
    std::string lhs;
    std::vector<std::string> rhs;
    int line = 0;
    std::string precSymbol;
    bool explicitPrec = false;

    std::string toString() const;
};

class Grammar {
public:
    bool load(const std::string& path, std::string& errorMessage);
    bool loadFromString(const std::string& text, const std::string& sourceName,
                        const std::string& baseDir, std::string& errorMessage);

    const std::vector<Production>& productions() const { return productions_; }
    const Production& production(int id) const { return productions_[(size_t)id]; }

    const std::vector<std::string>& terminals() const { return terminals_; }
    const std::vector<std::string>& nonterminals() const { return nonterminals_; }

    int symbolId(const std::string& symbol) const;
    const std::vector<std::string>& symbols() const { return symbols_; }

    bool isTerminal(const std::string& symbol) const;
    bool isNonterminal(const std::string& symbol) const;

    const std::string& startSymbol() const { return startSymbol_; }
    const std::string& augmentedStart() const { return augmentedStart_; }

    const std::vector<int>& productionsOf(const std::string& lhs) const;

    bool isEpsilonProduction(int id) const { return productions_[(size_t)id].rhs.empty(); }

    // Precedence (%left / %right / %nonassoc, %prec)
    bool hasPrecedence() const { return !precLevel_.empty(); }
    int precedenceOf(const std::string& symbol) const;
    Assoc assocOf(const std::string& symbol) const;
    int productionPrecedence(int id) const;

    // Other directives
    std::string displayName(const std::string& symbol) const;
    bool hasDisplayName(const std::string& symbol) const;
    const std::vector<std::string>& syncTokens() const { return sync_; }
    const std::string& recoverSymbol() const { return recover_; }
    const std::string& lexerSpec() const { return lexerSpec_; }
    const std::string& semantics() const { return semantics_; }
    const std::vector<std::string>& warnings() const { return warnings_; }
    const std::string& sourceName() const { return sourceName_; }

    void print(std::ostream& os) const;

private:
    bool parseDirective(const std::string& line, int lineNo, std::string& errorMessage);
    void computeWarnings(const std::map<std::string, int>& firstLine);

    std::vector<Production> productions_;
    std::vector<std::string> terminals_;
    std::vector<std::string> nonterminals_;
    std::set<std::string> terminalSet_;
    std::set<std::string> nonterminalSet_;
    std::string startSymbol_;
    std::string augmentedStart_;
    std::map<std::string, std::vector<int>> productionsOf_;
    std::vector<std::string> symbols_;
    std::map<std::string, int> symbolId_;

    std::map<std::string, int> precLevel_;
    std::map<std::string, Assoc> assoc_;
    std::vector<std::vector<std::string> > precLines_;
    std::vector<Assoc> precLineAssoc_;
    std::map<std::string, std::string> names_;
    std::map<std::string, int> nameLine_;
    std::vector<std::string> sync_;
    std::string recover_;
    int recoverLine_ = 0;
    std::string lexerSpec_;
    std::string semantics_;
    std::string baseDir_;
    std::string sourceName_;
    std::vector<std::string> warnings_;
};

#endif
