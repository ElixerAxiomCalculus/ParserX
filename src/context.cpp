#include "context.h"
#include "util.h"

using namespace std;

string withArticle(const string& noun) {
    if (noun.empty()) return noun;
    char c = (char)tolower(static_cast<unsigned char>(noun[0]));
    bool vowel = c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
    return (vowel ? "an " : "a ") + noun;
}

string describeExpected(const vector<string>& expected) {
    static const char* const kOperators[] = {"+", "-", "*", "/", "%", 0};
    vector<string> ops;
    vector<string> words;
    bool operandStart = false;
    for (size_t k = 0; k < expected.size(); ++k)
        if (expected[k] == "id" || expected[k] == "num" || expected[k] == "(") operandStart = true;
    string group = operandStart ? "a unary operator (" : "an operator (";
    for (size_t k = 0; k < expected.size(); ++k) {
        bool isOp = false;
        for (size_t o = 0; kOperators[o]; ++o)
            if (expected[k] == kOperators[o]) isOp = true;
        if (isOp) ops.push_back(expected[k]);
        else if (expected[k] == "$") words.push_back("end of input");
        else words.push_back("'" + expected[k] + "'");
    }
    if (ops.size() >= 2) words.push_back(group + utilJoin(ops, " ") + ")");
    else if (ops.size() == 1) words.push_back("'" + ops[0] + "'");
    if (words.empty()) return "nothing (no action in this state)";
    string out;
    for (size_t k = 0; k < words.size(); ++k) {
        if (k > 0) out += (k + 1 == words.size()) ? " or " : ", ";
        out += words[k];
    }
    return out;
}

namespace {

string render(const Grammar& g, const Production& p) {
    vector<string> words;
    for (size_t k = 0; k < p.rhs.size(); ++k)
        words.push_back(g.isNonterminal(p.rhs[k]) ? g.displayName(p.rhs[k]) : p.rhs[k]);
    return utilJoin(words, " ");
}

// An item describes an enclosing construct unless it is a unit production
// (F -> num) or the continuation of left recursion (E -> E . + T)
bool describesConstruct(const Grammar& g, const ItemRep& item) {
    const Production& p = g.production(item.prod);
    if (p.lhs == g.augmentedStart() || p.rhs.size() < 2 || item.dot == 0) return false;
    if (item.dot == 1 && p.rhs[0] == p.lhs) return false;
    return true;
}

}

string describeErrorContext(const Grammar& g, const ParsingTable& t, const ParseError& e) {
    if (e.stateStack.empty()) return "";
    string where;
    for (size_t k = e.stateStack.size(); k > 0 && where.empty(); --k) {
        const vector<ItemRep>& items = t.stateItems[(size_t)e.stateStack[k - 1]];
        for (size_t i = 0; i < items.size(); ++i) {
            if (!describesConstruct(g, items[i])) continue;
            const Production& p = g.production(items[i].prod);
            where = "in " + withArticle(g.displayName(p.lhs)) + " \"" + render(g, p) + "\"";
            break;
        }
    }
    string after;
    const string& top = e.symbolStack.empty() ? string("$") : e.symbolStack.back();
    if (top == "$") after = "at the start of the input";
    else if (g.isNonterminal(top)) after = "after " + withArticle(g.displayName(top));
    else after = "after '" + (e.previousLexeme.empty() ? top : e.previousLexeme) + "'";
    string text = where.empty() ? after : where + ", " + after;
    return text + ": expected " + describeExpected(e.expected);
}
