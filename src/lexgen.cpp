#include "lexgen.h"
#include "util.h"

#include <sstream>

using namespace std;

bool GeneratedLexer::loadSpec(const string& path, string& error) {
    string text;
    if (!utilReadFile(path, text, error)) {
        error = "cannot open lexer spec: " + path;
        return false;
    }
    if (!loadSpecFromString(text, error)) {
        error = path + ": " + error;
        return false;
    }
    return true;
}

// Spec parsing and automaton construction
bool GeneratedLexer::loadSpecFromString(const string& text, string& error) {
    rules_.clear();
    keywords_.clear();
    Nfa nfa;
    nfa.states.push_back(NfaState());
    nfa.start = 0;

    istringstream in(text);
    string line;
    int lineNo = 0;
    while (getline(in, line)) {
        ++lineNo;
        if (lineNo == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
        line = utilTrim(line);
        if (line.empty() || line[0] == '#') continue;
        string at = "line " + to_string(lineNo) + ": ";

        size_t sp = line.find_first_of(" \t");
        string head = line.substr(0, sp);
        string rest = sp == string::npos ? "" : utilTrim(line.substr(sp));

        vector<LexRule> fresh;
        if (head == "%keywords") {
            vector<string> w = utilSplitWhitespace(rest);
            if (w.empty()) {
                error = at + "%keywords needs at least one word";
                return false;
            }
            for (size_t k = 0; k < w.size(); ++k) keywords_.insert(w[k]);
            continue;
        } else if (head == "%literals") {
            vector<string> w = utilSplitWhitespace(rest);
            if (w.empty()) {
                error = at + "%literals needs at least one token";
                return false;
            }
            for (size_t k = 0; k < w.size(); ++k) {
                LexRule r;
                r.name = w[k];
                r.pattern = w[k];
                r.literal = true;
                r.line = lineNo;
                fresh.push_back(r);
            }
        } else if (head == "%skip") {
            if (rest.empty()) {
                error = at + "%skip needs a pattern";
                return false;
            }
            LexRule r;
            r.name = "%skip";
            r.pattern = rest;
            r.skip = true;
            r.line = lineNo;
            fresh.push_back(r);
        } else if (head[0] == '%' && head.size() > 1) {
            error = at + "unknown directive '" + head + "'";
            return false;
        } else {
            LexRule r;
            r.name = head;
            r.pattern = rest.empty() ? head : rest;
            r.literal = rest.empty();
            r.line = lineNo;
            fresh.push_back(r);
        }

        for (size_t k = 0; k < fresh.size(); ++k) {
            int idx = (int)rules_.size();
            int start;
            if (fresh[k].literal) {
                start = literalToNfa(fresh[k].pattern, idx, nfa);
            } else {
                string why;
                start = regexToNfa(fresh[k].pattern, idx, nfa, why);
                if (start < 0) {
                    error = at + "invalid pattern for '" + fresh[k].name + "': " + why;
                    return false;
                }
            }
            if (nfaFragmentMatchesEmpty(nfa, start)) {
                error = at + "rule '" + fresh[k].name + "' can match the empty string";
                return false;
            }
            nfa.states[0].eps.push_back(start);
            rules_.push_back(fresh[k]);
        }
    }
    if (rules_.empty()) {
        error = "lexer spec contains no rules";
        return false;
    }
    nfaStates_ = (int)nfa.states.size();
    Dfa raw = nfaToDfa(nfa);
    dfaStates_ = raw.size();
    dfa_ = minimizeDfa(raw);
    return true;
}

// Longest match, then earliest rule
LexResult GeneratedLexer::tokenize(const string& source) const {
    LexResult result;
    int line = 1;
    int col = 1;
    size_t i = 0;
    const size_t n = source.size();
    if (source.compare(0, 3, "\xEF\xBB\xBF") == 0) i = 3;

    while (i < n) {
        int state = dfa_.start;
        int bestRule = -1;
        size_t bestEnd = i;
        for (size_t j = i; j < n; ++j) {
            state = dfa_.next[(size_t)state][(unsigned char)source[j]];
            if (state < 0) break;
            int acc = dfa_.accept[(size_t)state];
            if (acc >= 0) {
                bestRule = acc;
                bestEnd = j + 1;
            }
        }
        int startLine = line;
        int startCol = col;
        if (bestRule < 0) {
            LexError e;
            e.line = startLine;
            e.column = startCol;
            e.character = source[i];
            result.errors.push_back(e);
            bestEnd = i + 1;
        }
        for (size_t k = i; k < bestEnd; ++k) {
            if (source[k] == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
        }
        if (bestRule >= 0 && !rules_[(size_t)bestRule].skip) {
            Token t;
            t.lexeme = source.substr(i, bestEnd - i);
            t.type = keywords_.count(t.lexeme) ? t.lexeme : rules_[(size_t)bestRule].name;
            t.line = startLine;
            t.column = startCol;
            result.tokens.push_back(t);
        }
        i = bestEnd;
    }

    Token end;
    end.type = "$";
    end.lexeme = "$";
    end.line = line;
    end.column = col;
    result.tokens.push_back(end);
    return result;
}

void GeneratedLexer::dumpDfa(ostream& os) const {
    vector<string> names;
    for (size_t k = 0; k < rules_.size(); ++k)
        names.push_back(rules_[k].skip ? "(skip)" : rules_[k].name);
    printDfa(os, dfa_, names);
}
