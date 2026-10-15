#include "grammar.h"
#include "util.h"

#include <fstream>
#include <sstream>

using namespace std;

string Production::toString() const {
    string s = lhs + " -> ";
    if (rhs.empty()) return s + "eps";
    for (size_t i = 0; i < rhs.size(); ++i) {
        if (i) s += " ";
        s += rhs[i];
    }
    return s;
}

static bool isEpsilonToken(const string& t) {
    return t == "eps" || t == "epsilon" || t == "empty" || t == "\xCE\xB5";
}

static bool allPipes(const string& w) {
    for (size_t k = 0; k < w.size(); ++k)
        if (w[k] != '|') return false;
    return !w.empty();
}

static string assocName(Assoc a) {
    switch (a) {
        case Assoc::LEFT: return "%left";
        case Assoc::RIGHT: return "%right";
        case Assoc::NONASSOC: return "%nonassoc";
        default: return "";
    }
}

// Split a directive line into words; "quoted text" is one word.
static bool splitDirectiveWords(const string& line, vector<string>& out) {
    size_t i = 0;
    while (i < line.size()) {
        if (isspace(static_cast<unsigned char>(line[i]))) {
            ++i;
            continue;
        }
        if (line[i] == '"') {
            size_t close = line.find('"', i + 1);
            if (close == string::npos) return false;
            out.push_back(line.substr(i, close - i + 1));
            i = close + 1;
            continue;
        }
        size_t j = i;
        while (j < line.size() && !isspace(static_cast<unsigned char>(line[j]))) ++j;
        out.push_back(line.substr(i, j - i));
        i = j;
    }
    return true;
}

int Grammar::symbolId(const string& symbol) const {
    map<string, int>::const_iterator it = symbolId_.find(symbol);
    return it == symbolId_.end() ? -1 : it->second;
}

bool Grammar::isTerminal(const string& symbol) const {
    return terminalSet_.count(symbol) > 0;
}

bool Grammar::isNonterminal(const string& symbol) const {
    return nonterminalSet_.count(symbol) > 0;
}

const vector<int>& Grammar::productionsOf(const string& lhs) const {
    static const vector<int> kEmpty;
    map<string, vector<int> >::const_iterator it = productionsOf_.find(lhs);
    if (it == productionsOf_.end()) return kEmpty;
    return it->second;
}

int Grammar::precedenceOf(const string& symbol) const {
    map<string, int>::const_iterator it = precLevel_.find(symbol);
    return it == precLevel_.end() ? 0 : it->second;
}

Assoc Grammar::assocOf(const string& symbol) const {
    map<string, Assoc>::const_iterator it = assoc_.find(symbol);
    return it == assoc_.end() ? Assoc::NONE : it->second;
}

int Grammar::productionPrecedence(int id) const {
    const Production& p = productions_[(size_t)id];
    if (p.precSymbol.empty()) return 0;
    return precedenceOf(p.precSymbol);
}

bool Grammar::hasDisplayName(const string& symbol) const {
    return names_.count(symbol) > 0;
}

string Grammar::displayName(const string& symbol) const {
    map<string, string>::const_iterator it = names_.find(symbol);
    return it == names_.end() ? symbol : it->second;
}

bool Grammar::load(const string& path, string& errorMessage) {
    string text;
    if (!utilReadFile(path, text, errorMessage)) {
        errorMessage = "cannot open grammar file: " + path;
        return false;
    }
    return loadFromString(text, path, utilDirName(path), errorMessage);
}

// Directives
bool Grammar::parseDirective(const string& line, int lineNo, string& errorMessage) {
    string at = "line " + to_string(lineNo) + ": ";
    vector<string> w;
    if (!splitDirectiveWords(line, w)) {
        errorMessage = at + "unterminated quoted string";
        return false;
    }
    const string& d = w[0];
    if (d == "%left" || d == "%right" || d == "%nonassoc") {
        Assoc a = d == "%left" ? Assoc::LEFT : d == "%right" ? Assoc::RIGHT : Assoc::NONASSOC;
        if (w.size() < 2) {
            errorMessage = at + d + " needs at least one symbol";
            return false;
        }
        int level = (int)precLines_.size() + 1;
        vector<string> syms;
        for (size_t k = 1; k < w.size(); ++k) {
            if (precLevel_.count(w[k])) {
                errorMessage = at + "terminal '" + w[k] + "' declared twice in precedence directives";
                return false;
            }
            precLevel_[w[k]] = level;
            assoc_[w[k]] = a;
            syms.push_back(w[k]);
        }
        precLines_.push_back(syms);
        precLineAssoc_.push_back(a);
    } else if (d == "%name") {
        if (w.size() != 3 || w[2].size() < 2 || w[2][0] != '"') {
            errorMessage = at + "expected %name SYMBOL \"text\"";
            return false;
        }
        names_[w[1]] = w[2].substr(1, w[2].size() - 2);
        nameLine_[w[1]] = lineNo;
    } else if (d == "%sync") {
        if (w.size() < 2) {
            errorMessage = at + "%sync needs at least one token";
            return false;
        }
        for (size_t k = 1; k < w.size(); ++k) sync_.push_back(w[k]);
    } else if (d == "%recover") {
        if (w.size() != 2) {
            errorMessage = at + "expected %recover NONTERMINAL";
            return false;
        }
        recover_ = w[1];
        recoverLine_ = lineNo;
    } else if (d == "%lexer") {
        if (w.size() != 2) {
            errorMessage = at + "expected %lexer FILE";
            return false;
        }
        bool absolute = w[1][0] == '/' || w[1][0] == '\\' ||
                        (w[1].size() > 1 && w[1][1] == ':');
        lexerSpec_ = (absolute || baseDir_.empty() || baseDir_ == ".")
                         ? w[1] : baseDir_ + "/" + w[1];
    } else if (d == "%semantics") {
        if (w.size() != 2) {
            errorMessage = at + "expected %semantics NAME";
            return false;
        }
        semantics_ = w[1];
    } else {
        errorMessage = at + "unknown directive '" + d + "'";
        return false;
    }
    return true;
}

// Load grammar
bool Grammar::loadFromString(const string& text, const string& sourceName,
                             const string& baseDir, string& errorMessage) {
    *this = Grammar();
    baseDir_ = baseDir;
    sourceName_ = sourceName;

    struct Raw {
        string lhs;
        vector<string> rhs;
        int line;
        string prec;
    };
    struct PendingPrec {
        string symbol;
        int line;
    };
    vector<Raw> raw;
    vector<PendingPrec> pending;
    set<string> seenProductions;
    map<string, int> firstLine;
    istringstream in(text);
    string line;
    int lineNo = 0;
    while (getline(in, line)) {
        ++lineNo;
        if (lineNo == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
        size_t hash = line.find('#');
        if (hash != string::npos) line = line.substr(0, hash);
        line = utilTrim(line);
        if (line.empty()) continue;
        string at = "line " + to_string(lineNo) + ": ";

        if (line[0] == '%') {
            if (!parseDirective(line, lineNo, errorMessage)) return false;
            continue;
        }

        size_t arrow = line.find("->");
        size_t arrowLen = 2;
        if (arrow == string::npos) {
            arrow = line.find("\xE2\x86\x92");
            arrowLen = 3;
        }
        if (arrow == string::npos) {
            errorMessage = at + "expected '->' in production";
            return false;
        }
        string lhs = utilTrim(line.substr(0, arrow));
        string rhs = utilTrim(line.substr(arrow + arrowLen));
        if (lhs.empty() || lhs.find_first_of(" \t") != string::npos) {
            errorMessage = at + "left-hand side must be a single symbol";
            return false;
        }
        if (!firstLine.count(lhs)) firstLine[lhs] = lineNo;

        // Split alternatives: a lone '|' separates; '||' is a symbol
        vector<vector<string> > alternatives(1);
        vector<string> words = utilSplitWhitespace(rhs);
        for (size_t k = 0; k < words.size(); ++k) {
            const string& word = words[k];
            if (word == "|") {
                alternatives.push_back(vector<string>());
            } else if (word.find('|') != string::npos && !allPipes(word)) {
                string cur;
                for (size_t c = 0; c <= word.size(); ++c) {
                    if (c == word.size() || word[c] == '|') {
                        if (!cur.empty()) alternatives.back().push_back(cur);
                        cur.clear();
                        if (c < word.size()) alternatives.push_back(vector<string>());
                    } else {
                        cur += word[c];
                    }
                }
            } else {
                alternatives.back().push_back(word);
            }
        }

        for (size_t a = 0; a < alternatives.size(); ++a) {
            const vector<string>& symbols = alternatives[a];
            vector<string> cleaned;
            bool sawEps = false;
            string prec;
            for (size_t k = 0; k < symbols.size(); ++k) {
                if (symbols[k] == "%prec") {
                    if (k + 2 != symbols.size()) {
                        errorMessage = at + "%prec must be followed by one symbol at the "
                                            "end of an alternative";
                        return false;
                    }
                    prec = symbols[k + 1];
                    PendingPrec pp;
                    pp.symbol = prec;
                    pp.line = lineNo;
                    pending.push_back(pp);
                    break;
                }
                if (isEpsilonToken(symbols[k])) sawEps = true;
                else cleaned.push_back(symbols[k]);
            }
            if (sawEps && !cleaned.empty()) {
                errorMessage = at + "'eps' cannot be combined with other symbols";
                return false;
            }
            if (sawEps) cleaned.clear();
            Raw r;
            r.lhs = lhs;
            r.rhs = cleaned;
            r.line = lineNo;
            r.prec = prec;
            string key = lhs + " ->";
            for (size_t k = 0; k < cleaned.size(); ++k) key += " " + cleaned[k];
            if (!seenProductions.insert(key).second) {
                Production tmp;
                tmp.lhs = lhs;
                tmp.rhs = cleaned;
                warnings_.push_back("line " + to_string(lineNo) + ": duplicate production " +
                                    tmp.toString() + " (ignored)");
                continue;
            }
            raw.push_back(r);
        }
    }
    if (raw.empty()) {
        errorMessage = "grammar file contains no productions: " + sourceName;
        return false;
    }

    // Classify symbols
    for (size_t k = 0; k < raw.size(); ++k) {
        if (nonterminalSet_.insert(raw[k].lhs).second) nonterminals_.push_back(raw[k].lhs);
    }
    set<string> seen;
    for (size_t k = 0; k < raw.size(); ++k) {
        for (size_t j = 0; j < raw[k].rhs.size(); ++j) {
            const string& s = raw[k].rhs[j];
            if (nonterminalSet_.count(s)) continue;
            if (seen.insert(s).second) {
                terminalSet_.insert(s);
                terminals_.push_back(s);
            }
        }
    }

    // Check directives that name symbols
    for (size_t k = 0; k < pending.size(); ++k) {
        if (!precLevel_.count(pending[k].symbol)) {
            errorMessage = "line " + to_string(pending[k].line) + ": %prec uses '" +
                           pending[k].symbol + "', which has no precedence declaration";
            return false;
        }
    }
    for (map<string, int>::const_iterator it = precLevel_.begin(); it != precLevel_.end(); ++it) {
        if (nonterminalSet_.count(it->first)) {
            errorMessage = "precedence declared for non-terminal '" + it->first + "'";
            return false;
        }
    }
    for (map<string, int>::const_iterator it = nameLine_.begin(); it != nameLine_.end(); ++it) {
        if (!nonterminalSet_.count(it->first)) {
            errorMessage = "line " + to_string(it->second) + ": %name for '" + it->first +
                           "', which is not a non-terminal";
            return false;
        }
    }
    if (!recover_.empty() && !nonterminalSet_.count(recover_)) {
        errorMessage = "line " + to_string(recoverLine_) + ": %recover needs a non-terminal, got '" +
                       recover_ + "'";
        return false;
    }

    startSymbol_ = raw.front().lhs;

    // Augment grammar
    augmentedStart_ = startSymbol_ + "'";
    Production aug;
    aug.id = 0;
    aug.lhs = augmentedStart_;
    aug.rhs.push_back(startSymbol_);
    productions_.push_back(aug);
    int nextId = 1;
    for (size_t k = 0; k < raw.size(); ++k) {
        Production p;
        p.id = nextId++;
        p.lhs = raw[k].lhs;
        p.rhs = raw[k].rhs;
        p.line = raw[k].line;
        if (!raw[k].prec.empty()) {
            p.precSymbol = raw[k].prec;
            p.explicitPrec = true;
        } else {
            for (size_t j = p.rhs.size(); j > 0; --j) {
                if (terminalSet_.count(p.rhs[j - 1])) {
                    p.precSymbol = p.rhs[j - 1];
                    break;
                }
            }
        }
        productions_.push_back(p);
    }

    nonterminalSet_.insert(augmentedStart_);
    nonterminals_.insert(nonterminals_.begin(), augmentedStart_);

    terminalSet_.insert("$");
    terminals_.push_back("$");

    for (size_t k = 0; k < productions_.size(); ++k) {
        productionsOf_[productions_[k].lhs].push_back(productions_[k].id);
    }

    // Symbol ids
    for (size_t k = 0; k < nonterminals_.size(); ++k) symbols_.push_back(nonterminals_[k]);
    for (size_t k = 0; k < terminals_.size(); ++k) symbols_.push_back(terminals_[k]);
    for (size_t k = 0; k < symbols_.size(); ++k) symbolId_[symbols_[k]] = (int)k;

    computeWarnings(firstLine);
    return true;
}

// Unproductive and unreachable non-terminals
void Grammar::computeWarnings(const map<string, int>& firstLine) {
    set<string> productive;
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t k = 1; k < productions_.size(); ++k) {
            const Production& p = productions_[k];
            if (productive.count(p.lhs)) continue;
            bool ok = true;
            for (size_t i = 0; i < p.rhs.size(); ++i) {
                if (nonterminalSet_.count(p.rhs[i]) && !productive.count(p.rhs[i])) {
                    ok = false;
                    break;
                }
            }
            if (ok) {
                productive.insert(p.lhs);
                changed = true;
            }
        }
    }
    set<string> reachable;
    vector<string> work(1, startSymbol_);
    reachable.insert(startSymbol_);
    while (!work.empty()) {
        string a = work.back();
        work.pop_back();
        const vector<int>& prods = productionsOf(a);
        for (size_t k = 0; k < prods.size(); ++k) {
            const Production& p = productions_[(size_t)prods[k]];
            for (size_t i = 0; i < p.rhs.size(); ++i) {
                if (nonterminalSet_.count(p.rhs[i]) && reachable.insert(p.rhs[i]).second)
                    work.push_back(p.rhs[i]);
            }
        }
    }
    for (size_t k = 1; k < nonterminals_.size(); ++k) {
        const string& a = nonterminals_[k];
        map<string, int>::const_iterator ln = firstLine.find(a);
        string at = ln == firstLine.end() ? "" : "line " + to_string(ln->second) + ": ";
        if (!productive.count(a))
            warnings_.push_back(at + "non-terminal " + a +
                                " cannot derive any terminal string (unproductive)");
        if (!reachable.count(a))
            warnings_.push_back(at + "non-terminal " + a +
                                " is unreachable from the start symbol");
    }
}

void Grammar::print(ostream& os) const {
    os << "Start symbol: " << startSymbol_ << "\n";
    os << "Augmented start production (0): " << augmentedStart_
       << " -> " << startSymbol_ << "\n";
    os << "Non-terminals (" << nonterminals_.size() << "): "
       << utilJoin(nonterminals_, ", ") << "\n";
    os << "Terminals     (" << terminals_.size() << "): "
       << utilJoin(terminals_, ", ") << "\n";
    os << "Symbol ids    :";
    for (size_t k = 0; k < symbols_.size(); ++k) os << " " << symbols_[k] << "=" << k;
    os << "\n";
    os << "Productions (" << productions_.size() << "):\n";
    for (size_t k = 0; k < productions_.size(); ++k) {
        os << "  (" << productions_[k].id << ") "
           << productions_[k].toString();
        if (productions_[k].explicitPrec)
            os << "   %prec " << productions_[k].precSymbol;
        os << "\n";
    }
    if (!precLines_.empty()) {
        os << "Precedence (lowest first):\n";
        for (size_t k = 0; k < precLines_.size(); ++k)
            os << "  level " << (k + 1) << "  " << assocName(precLineAssoc_[k]) << "  "
               << utilJoin(precLines_[k], " ") << "\n";
    }
    for (map<string, string>::const_iterator it = names_.begin(); it != names_.end(); ++it)
        os << "Name: " << it->first << " = \"" << it->second << "\"\n";
    if (!sync_.empty()) os << "Sync tokens: " << utilJoin(sync_, " ") << "\n";
    if (!recover_.empty()) os << "Recover to: " << recover_ << "\n";
    if (!lexerSpec_.empty()) os << "Lexer spec: " << lexerSpec_ << "\n";
    if (!semantics_.empty()) os << "Semantics: " << semantics_ << "\n";
    for (size_t k = 0; k < warnings_.size(); ++k)
        os << "Warning: " << warnings_[k] << "\n";
}
