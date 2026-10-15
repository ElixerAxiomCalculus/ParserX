#include "parser.h"
#include "compress.h"
#include "util.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

using namespace std;

namespace {

const int kMaxRecoveriesAtOneToken = 10;

void popTo(vector<int>& states, vector<string>& symbols, vector<int>& nodes, size_t size) {
    states.resize(size);
    symbols.resize(size);
    if (!nodes.empty()) nodes.resize(size);
}

int addNode(ParseTree& tree, const string& symbol, int prod, int token) {
    TreeNode n;
    n.symbol = symbol;
    n.prod = prod;
    n.token = token;
    tree.nodes.push_back(n);
    return (int)tree.nodes.size() - 1;
}

}

Parser::Parser(const Grammar& g, const ParsingTable& table, const PackedTable* packed)
    : g_(g), table_(table), packed_(packed) {}

// Table access (raw or compressed)
bool Parser::action(int state, const string& symbol, Action& out) const {
    if (packed_) return packed_->action(state, symbol, out);
    const Action* a = table_.findAction(state, symbol);
    if (!a) return false;
    out = *a;
    return true;
}

int Parser::gotoState(int state, const string& nonterminal) const {
    if (packed_) return packed_->gotoState(state, nonterminal);
    map<int, map<string, int> >::const_iterator row = table_.gotos.find(state);
    if (row == table_.gotos.end()) return -1;
    map<string, int>::const_iterator it = row->second.find(nonterminal);
    return it == row->second.end() ? -1 : it->second;
}

vector<string> Parser::expected(int state) const {
    if (packed_) return packed_->expected(state);
    vector<string> out;
    map<int, map<string, Action> >::const_iterator row = table_.action.find(state);
    if (row == table_.action.end()) return out;
    for (map<string, Action>::const_iterator a = row->second.begin(); a != row->second.end(); ++a)
        out.push_back(a->first);
    return out;
}

ParseError Parser::makeError(const vector<int>& states, const vector<string>& symbols,
                             const vector<Token>& tokens, size_t pos) const {
    int state = states.back();
    const Token& tok = pos < tokens.size() ? tokens[pos] : tokens.back();
    ParseError e;
    e.hasError = true;
    e.parserName = table_.name;
    e.state = state;
    e.unexpectedType = tok.type;
    e.unexpectedLexeme = tok.lexeme;
    e.line = tok.line;
    e.column = tok.column;
    e.tokenIndex = (int)pos;
    e.stateStack = states;
    e.symbolStack = symbols;
    if (pos > 0 && pos <= tokens.size()) e.previousLexeme = tokens[pos - 1].lexeme;
    e.expected = expected(state);
    e.reason = "no " + table_.name + " action for state " + to_string(state) + " on '" +
               tok.type + "'";
    return e;
}

// Non-terminal used by panic-mode recovery
string Parser::recoverSymbol() const {
    if (!g_.recoverSymbol().empty()) return g_.recoverSymbol();
    vector<string> sync = g_.syncTokens();
    if (sync.empty()) {
        sync.push_back(";");
        sync.push_back("}");
    }
    const vector<Production>& prods = g_.productions();
    for (size_t k = 1; k < prods.size(); ++k) {
        if (prods[k].rhs.empty()) continue;
        for (size_t s = 0; s < sync.size(); ++s)
            if (prods[k].rhs.back() == sync[s]) return prods[k].lhs;
    }
    return g_.startSymbol();
}

ParseResult Parser::parse(const vector<Token>& tokens, bool recordTrace) const {
    ParseOptions opt;
    opt.recordTrace = recordTrace;
    return parse(tokens, opt);
}

// Parse loop
ParseResult Parser::parse(const vector<Token>& tokens, const ParseOptions& opt) const {
    ParseResult r;
    vector<int> states(1, 0);
    vector<string> symbols(1, "$");
    vector<int> nodes;
    if (opt.buildTree) nodes.push_back(-1);

    bool errorProductions = g_.isTerminal("error");
    set<string> sync(g_.syncTokens().begin(), g_.syncTokens().end());
    if (sync.empty()) {
        sync.insert(";");
        sync.insert("}");
    }
    string recoverNt = recoverSymbol();
    if (opt.recover) r.recoveryMode = errorProductions ? "error productions" : "panic mode";

    size_t pos = 0;
    int stepNo = 0;
    int reducesOnToken = 0;
    int suppress = 0;
    size_t lastRecoveryPos = (size_t)-1;
    int recoveriesHere = 0;
    const size_t kGuard = 100000 + tokens.size() * 64;
    while (stepNo < (int)kGuard) {
        ParseStep step;
        step.number = stepNo;
        if (opt.recordTrace) {
            step.stateStack = states;
            step.symbolStack = symbols;
            for (size_t k = pos; k < tokens.size(); ++k) step.remaining.push_back(tokens[k]);
        }

        int state = states.back();
        const Token& tok = (pos < tokens.size()) ? tokens[pos] : tokens.back();
        Action act;
        if (!action(state, tok.type, act)) {
            ParseError e = makeError(states, symbols, tokens, pos);
            if (!r.error.hasError) {
                step.action = "ERROR";
                if (opt.recordTrace) r.steps.push_back(step);
                r.errorStep = stepNo;
                r.reductionsBeforeError = reducesOnToken;
                r.error = e;
            }
            r.stepCount = stepNo + 1;
            if (!opt.recover) return r;

            // Error recovery
            bool report = suppress == 0;
            if (report) r.errors.push_back(e);
            if (report && (int)r.errors.size() >= opt.maxErrors) {
                r.abortReason = "error limit of " + to_string(opt.maxErrors) + " reached";
                return r;
            }
            if (pos == lastRecoveryPos) {
                if (++recoveriesHere > kMaxRecoveriesAtOneToken) {
                    r.abortReason = "recovery made no progress at token " + to_string(pos);
                    return r;
                }
            } else {
                lastRecoveryPos = pos;
                recoveriesHere = 0;
            }
            ++stepNo;
            reducesOnToken = 0;
            if (errorProductions) {
                if (!report) {
                    if (tok.type == "$") {
                        r.abortReason = "end of input reached during recovery";
                        return r;
                    }
                    ++pos;
                    ++r.skippedTokens;
                    continue;
                }
                Action shiftError;
                while (!states.empty() &&
                       !(action(states.back(), "error", shiftError) &&
                         shiftError.type == ActionType::SHIFT))
                    popTo(states, symbols, nodes, states.size() - 1);
                if (states.empty()) {
                    r.abortReason = "no state on the stack can shift 'error'";
                    return r;
                }
                states.push_back(shiftError.value);
                symbols.push_back("error");
                if (opt.buildTree) nodes.push_back(addNode(r.tree, "error", -1, -1));
                suppress = 3;
                Action probe;
                while (!action(states.back(), tokens[pos].type, probe)) {
                    if (tokens[pos].type == "$") {
                        r.abortReason = "end of input reached during recovery";
                        return r;
                    }
                    ++pos;
                    ++r.skippedTokens;
                }
            } else {
                while (!sync.count(tokens[pos].type)) {
                    if (tokens[pos].type == "$") {
                        r.abortReason = "end of input reached while looking for a sync token";
                        return r;
                    }
                    ++pos;
                    ++r.skippedTokens;
                }
                ++pos;
                ++r.skippedTokens;
                while (!states.empty() && gotoState(states.back(), recoverNt) < 0)
                    popTo(states, symbols, nodes, states.size() - 1);
                if (states.empty()) {
                    r.abortReason = "no state on the stack has a GOTO on " + recoverNt;
                    return r;
                }
                states.push_back(gotoState(states.back(), recoverNt));
                symbols.push_back(recoverNt);
                if (opt.buildTree) nodes.push_back(addNode(r.tree, recoverNt, -1, -1));
            }
            continue;
        }

        if (table_.conflictCells.count(make_pair(state, tok.type))) {
            ++r.conflictMoves;
            r.conflictStates.insert(state);
        }

        if (act.type == ActionType::SHIFT) {
            step.action = "shift " + to_string(act.value);
            if (opt.recordTrace) r.steps.push_back(step);
            states.push_back(act.value);
            symbols.push_back(tok.type);
            if (opt.buildTree) nodes.push_back(addNode(r.tree, tok.type, -1, (int)pos));
            if (pos < tokens.size()) ++pos;
            if (suppress > 0) --suppress;
            reducesOnToken = 0;
            ++stepNo;
        } else if (act.type == ActionType::REDUCE) {
            const Production& p = g_.production(act.value);
            if (opt.recordTrace) {
                ostringstream desc;
                desc << "reduce " << p.id << " (" << p.toString() << ")";
                step.action = desc.str();
                step.reduceProd = p.id;
                r.steps.push_back(step);
            }
            size_t keep = states.size() - p.rhs.size();
            int node = -1;
            if (opt.buildTree) {
                node = addNode(r.tree, p.lhs, p.id, -1);
                r.tree.nodes[(size_t)node].kids.assign(nodes.begin() + (long)keep, nodes.end());
            }
            popTo(states, symbols, nodes, keep);
            int target = gotoState(states.back(), p.lhs);
            if (target < 0) {
                ParseError e = makeError(states, symbols, tokens, pos);
                e.expected.clear();
                e.reason = "missing GOTO[" + to_string(states.back()) + "][" + p.lhs +
                           "] after reducing " + p.toString();
                r.stepCount = stepNo + 1;
                r.errorStep = stepNo;
                r.reductionsBeforeError = reducesOnToken;
                r.error = e;
                r.abortReason = e.reason;
                return r;
            }
            states.push_back(target);
            symbols.push_back(p.lhs);
            if (opt.buildTree) nodes.push_back(node);
            ++reducesOnToken;
            ++stepNo;
        } else {
            step.action = "ACCEPT";
            if (opt.recordTrace) r.steps.push_back(step);
            r.stepCount = stepNo + 1;
            r.accepted = !r.error.hasError;
            if (opt.buildTree && nodes.size() > 1) r.tree.root = nodes.back();
            return r;
        }
    }
    ParseError e;
    e.hasError = true;
    e.parserName = table_.name;
    e.reason = "parse step limit exceeded (possible grammar loop)";
    r.stepCount = stepNo;
    r.errorStep = stepNo;
    r.error = e;
    r.abortReason = e.reason;
    r.accepted = false;
    return r;
}

// Nodes are created children-first, so one pass in index order builds every string
string treeToCompactString(const ParseTree& tree, const vector<Token>& tokens) {
    if (tree.root < 0) return "(tree unavailable)";
    vector<string> text(tree.nodes.size());
    for (size_t k = 0; k <= (size_t)tree.root; ++k) {
        const TreeNode& n = tree.nodes[k];
        if (n.prod < 0) {
            text[k] = n.token >= 0 ? tokens[(size_t)n.token].lexeme : n.symbol;
        } else if (n.kids.size() == 1) {
            text[k] = text[(size_t)n.kids[0]];
        } else if (n.kids.empty()) {
            text[k] = "[" + n.symbol + "]";
        } else {
            string s = "[" + n.symbol;
            for (size_t c = 0; c < n.kids.size(); ++c) s += " " + text[(size_t)n.kids[c]];
            text[k] = s + "]";
        }
    }
    return text[(size_t)tree.root];
}

static string stackToString(const vector<int>& states) {
    ostringstream ss;
    for (size_t k = 0; k < states.size(); ++k) {
        if (k) ss << " ";
        ss << states[k];
    }
    return ss.str();
}

static string inputToString(const vector<Token>& toks) {
    ostringstream ss;
    for (size_t k = 0; k < toks.size(); ++k) {
        if (k) ss << " ";
        ss << toks[k].type;
    }
    return ss.str();
}

// Trace output
void printTrace(ostream& os, const Grammar& g, const ParseResult& r, bool full) {
    (void)g;
    if (r.steps.empty()) {
        os << "(empty trace)\n";
        return;
    }
    vector<ParseStep> steps = r.steps;
    bool truncated = false;
    if (!full && steps.size() > 24) {
        vector<ParseStep> shown;
        for (size_t k = 0; k < 12; ++k) shown.push_back(steps[k]);
        for (size_t k = steps.size() - 12; k < steps.size(); ++k) shown.push_back(steps[k]);
        steps = shown;
        truncated = true;
    }
    ios::fmtflags saved = os.flags();
    os << left
       << setw(5) << "STEP"
       << setw(26) << "STATE STACK"
       << setw(30) << "SYMBOL STACK"
       << setw(30) << "INPUT"
       << "ACTION\n";
    os << utilRepeat('-', 110) << "\n";
    size_t skippedAt = 12;
    for (size_t k = 0; k < steps.size(); ++k) {
        if (truncated && k == skippedAt) {
            os << "  ... (" << (r.steps.size() - 24)
               << " middle steps omitted; use --trace full) ...\n";
        }
        const ParseStep& s = steps[k];
        os << left
           << setw(5) << s.number
           << setw(26) << stackToString(s.stateStack).substr(0, 25)
           << setw(30) << utilJoin(s.symbolStack, " ").substr(0, 29)
           << setw(30) << inputToString(s.remaining).substr(0, 29)
           << s.action << "\n";
    }
    os.flags(saved);
}

// Error report; long inputs show only the lines around the error
void printParseError(ostream& os, const string& source,
                     const ParseResult& r) {
    const ParseError& e = r.error;
    os << "Syntax Error\n";
    os << "  Parser:          " << e.parserName << "\n";
    os << "  Line:            " << e.line << "\n";
    os << "  Column:          " << e.column << "\n";
    if (e.unexpectedType == "$")
        os << "  Unexpected:      end of input ($)\n";
    else
        os << "  Unexpected:      '" << e.unexpectedLexeme << "' (token " << e.unexpectedType
        << ")\n";
    os << "  Parser state:    " << e.state << "\n";
    if (!e.expected.empty())
        os << "  Expected one of: " << utilJoin(e.expected, ", ") << "\n";
    if (!e.context.empty()) os << "  Context:         " << e.context << "\n";
    if (!e.reason.empty()) os << "  Reason:          " << e.reason << "\n";
    if (source.empty()) return;
    vector<string> lines;
    istringstream in(source);
    string ln;
    while (getline(in, ln)) lines.push_back(ln);
    os << "  Input:\n";
    string caret = "    " + utilRepeat(' ', (int)(to_string(e.line).size() + 2 + e.column - 1)) + "^\n";
    if (lines.size() <= 20) {
        for (size_t k = 0; k < lines.size(); ++k) os << "    " << (k + 1) << ": " << lines[k] << "\n";
        if (e.line > 0) os << caret;
        return;
    }
    int from = max(1, e.line - 2);
    int to = min((int)lines.size(), e.line + 2);
    for (int n = from; n <= to; ++n) {
        os << "    " << n << ": " << lines[(size_t)(n - 1)] << "\n";
        if (n == e.line) os << caret;
    }
}

void printRecoveredErrors(ostream& os, const ParseResult& r) {
    os << "Recovery (" << r.recoveryMode << "): " << r.errors.size() << " error(s) reported, "
       << r.skippedTokens << " token(s) skipped";
    if (!r.abortReason.empty()) os << "; stopped: " << r.abortReason;
    os << "\n";
    for (size_t k = 0; k < r.errors.size(); ++k) {
        const ParseError& e = r.errors[k];
        os << "  Error " << (k + 1) << ": line " << e.line << ", column " << e.column << ": unexpected ";
        if (e.unexpectedType == "$") os << "end of input";
        else os << "'" << e.unexpectedLexeme << "'";
        os << " in state " << e.state << "; expected: " << utilJoin(e.expected, " ") << "\n";
    }
}
