// Unit tests for the pure functions of ParserX. No framework: CHECK records a
// failure with its line, and main() prints a summary and returns non-zero
// when anything failed.  Build and run with:  make unit

#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "clr.h"
#include "comparison.h"
#include "compress.h"
#include "explain.h"
#include "first_follow.h"
#include "fuzz.h"
#include "grammar.h"
#include "lalr_dp.h"
#include "lexgen.h"
#include "parser.h"
#include "regex.h"
#include "slr.h"
#include "table.h"

using namespace std;

namespace {

int checks = 0;
int failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++checks;                                                                \
        if (!(cond)) {                                                           \
            ++failures;                                                          \
            cout << "FAIL line " << __LINE__ << ": " #cond << "\n";              \
        }                                                                        \
    } while (0)

Grammar load(const string& text) {
    Grammar g;
    string err;
    if (!g.loadFromString(text, "unit", ".", err)) cout << "grammar error: " << err << "\n";
    return g;
}

set<string> S(const string& words) {
    istringstream in(words);
    set<string> out;
    string w;
    while (in >> w) out.insert(w);
    return out;
}

vector<Token> tokens(const string& types) {
    istringstream in(types);
    vector<Token> out;
    string w;
    while (in >> w) {
        Token t;
        t.type = w;
        t.lexeme = w;
        out.push_back(t);
    }
    Token end;
    end.type = "$";
    end.lexeme = "$";
    out.push_back(end);
    return out;
}

const char* kExpr = "E -> E + T | T\nT -> T * F | F\nF -> ( E ) | id\n";

// FIRST and FOLLOW
void testFirstFollow() {
    Grammar g = load(kExpr);
    FirstFollow ff(g);
    CHECK(ff.first("E") == S("( id"));
    CHECK(ff.first("T") == S("( id"));
    CHECK(ff.follow("E") == S("$ ) +"));
    CHECK(ff.follow("T") == S("$ ) * +"));
    CHECK(ff.follow("F") == S("$ ) * +"));
    CHECK(ff.follow("E'") == S("$"));

    Grammar e = load("S -> A B\nA -> a A | eps\nB -> b B | eps\n");
    FirstFollow fe(e);
    CHECK(fe.nullable("A") && fe.nullable("B") && fe.nullable("S"));
    CHECK(fe.first("S") == S("a b eps"));
    CHECK(fe.follow("A") == S("$ b"));
    vector<string> seq;
    seq.push_back("A");
    seq.push_back("B");
    CHECK(fe.firstOfSequence(seq) == S("a b eps"));

    // Provenance: '=' enters FOLLOW(R) through L -> * R from FOLLOW(L)
    Grammar lv = load("S -> L = R | R\nL -> * R | id\nR -> L\n");
    FirstFollow fl(lv);
    const FollowReason* why = fl.followReason("R", "=");
    CHECK(why && why->kind == FollowReason::FOLLOW_OF_LHS);
    CHECK(why && lv.production(why->prod).toString() == "L -> * R");
}

// LR(0) closure / goto through the SLR automaton
void testClosureGoto() {
    Grammar g = load(kExpr);
    LRMachine m = buildSLRMachine(g);
    CHECK(m.states.size() == 12);
    CHECK(m.states[0].items.size() == 7);
    CHECK(m.states[0].transitions.count("id") == 1);
    CHECK(m.states[0].transitions.count("+") == 0);
    FirstFollow ff(g);
    LRMachine clr = buildCLRMachine(g, ff);
    CHECK(clr.states.size() == 22);
}

// The digraph algorithm, including a cycle
void testDigraph() {
    vector<vector<int> > rel(4);
    rel[0].push_back(1);
    rel[1].push_back(2);
    rel[2].push_back(1);
    rel[3].push_back(0);
    vector<vector<char> > sets(4, vector<char>(4, 0));
    for (int k = 0; k < 4; ++k) sets[(size_t)k][(size_t)k] = 1;
    int sccs = 0;
    digraph(rel, sets, &sccs);
    CHECK(sccs == 1);
    CHECK(sets[1] == sets[2]);
    CHECK(sets[1][1] && sets[1][2] && !sets[1][0]);
    CHECK(sets[0][0] && sets[0][1] && sets[0][2] && !sets[0][3]);
    CHECK(sets[3][0] && sets[3][1] && sets[3][2] && sets[3][3]);
}

int runDfa(const Dfa& d, const string& s) {
    int st = d.start;
    for (size_t k = 0; k < s.size() && st >= 0; ++k) st = d.next[(size_t)st][(unsigned char)s[k]];
    return st < 0 ? -1 : d.accept[(size_t)st];
}

// Regex -> Thompson NFA -> subset DFA -> minimised DFA
void testRegex() {
    Nfa nfa;
    nfa.states.push_back(NfaState());
    nfa.start = 0;
    string err;
    int a = regexToNfa("(ab|c)*d", 0, nfa, err);
    int b = regexToNfa("[a-c]+", 1, nfa, err);
    CHECK(a >= 0 && b >= 0);
    nfa.states[0].eps.push_back(a);
    nfa.states[0].eps.push_back(b);
    Dfa d = nfaToDfa(nfa);
    Dfa m = minimizeDfa(d);
    CHECK(m.size() <= d.size());
    CHECK(runDfa(m, "ababd") == 0);
    CHECK(runDfa(m, "cd") == 0);
    CHECK(runDfa(m, "abc") == 1);
    CHECK(runDfa(m, "abx") == -1);
    CHECK(runDfa(d, "ababd") == runDfa(m, "ababd"));

    Nfa bad;
    bad.states.push_back(NfaState());
    CHECK(regexToNfa("[0-9", 0, bad, err) < 0);
    CHECK(err.find("unterminated character class") != string::npos);
    CHECK(regexToNfa("*a", 0, bad, err) < 0);
    CHECK(regexToNfa("(a", 0, bad, err) < 0);
    int e = regexToNfa("a*", 0, bad, err);
    CHECK(e >= 0 && nfaFragmentMatchesEmpty(bad, e));

    GeneratedLexer lx;
    CHECK(lx.loadSpecFromString("%skip [ ]+\nrelop <=|<\n%literals < + +=\n%keywords if\nid [a-z]+\n", err));
    LexResult r = lx.tokenize("a <= b += iffy if");
    CHECK(r.tokens.size() == 7);
    CHECK(r.tokens[1].type == "relop" && r.tokens[1].lexeme == "<=");
    CHECK(r.tokens[3].type == "+=");
    CHECK(r.tokens[4].type == "id" && r.tokens[5].type == "if");
    CHECK(r.tokens[4].column == 11);
}

// Precedence resolution of shift/reduce conflicts
void testPrecedence() {
    Grammar g = load("%left +\n%left *\n%nonassoc <\nE -> E + E | E * E | E < E | id\n");
    FirstFollow ff(g);
    ParsingTable t = buildParsingTable(g, ff, buildSLRMachine(g));
    CHECK(t.srConflicts() == 0);
    CHECK(t.srPrecConflicts() > 0);
    CHECK(t.nonassocErrors > 0);
    Parser p(g, t);
    CHECK(p.parse(tokens("id + id * id < id"), false).accepted);
    CHECK(!p.parse(tokens("id < id < id"), false).accepted);
    ParseOptions po;
    po.recordTrace = false;
    po.buildTree = true;
    ParseResult r = p.parse(tokens("id + id * id"), po);
    CHECK(treeToCompactString(r.tree, tokens("id + id * id")) == "[E id + [E id * id]]");
    r = p.parse(tokens("id + id + id"), po);
    CHECK(treeToCompactString(r.tree, tokens("id + id + id")) == "[E [E id + id] + id]");
}

// Comb-vector lookups equal raw lookups
void testCompression() {
    Grammar g = load(kExpr);
    FirstFollow ff(g);
    ParsingTable t = buildParsingTable(g, ff, buildCLRMachine(g, ff));
    PackedTable pt;
    pt.build(g, t);
    ostringstream os;
    CHECK(pt.verify(t, os));
    CHECK(pt.packedBytes() < pt.rawBytes());
    for (size_t s = 0; s < t.stateItems.size(); ++s) {
        for (size_t k = 0; k < g.terminals().size(); ++k) {
            const Action* raw = t.findAction((int)s, g.terminals()[k]);
            Action got;
            if (raw) CHECK(pt.action((int)s, g.terminals()[k], got) && got == *raw);
        }
    }
    Parser packed(g, t, &pt);
    CHECK(packed.parse(tokens("id * ( id + id )"), false).accepted);
    ParseResult bad = packed.parse(tokens("id + * id"), false);
    CHECK(!bad.accepted && bad.error.tokenIndex == 2);
}

// Witnesses drive the parser into the conflict state
void testWitness() {
    Grammar g = load("S -> L = R | R\nL -> * R | id\nR -> L\n");
    FirstFollow ff(g);
    LRMachine m = buildSLRMachine(g);
    ParsingTable t = buildParsingTable(g, ff, m);
    CHECK(t.conflicts.size() == 1);
    bool ok = false;
    int tried = 0;
    vector<string> w = findWitness(g, t, m, t.conflicts[0].state, "=", ok, tried);
    CHECK(ok);
    CHECK(w.size() == 1 && w[0] == "id");
    string cause;
    string chain = followChain(g, ff, "R", "=", &cause);
    CHECK(cause == "FOLLOW(R) <- FOLLOW(L) <- FIRST after L in S -> L = R");
    CHECK(chain.find("R appears at the end of L -> * R") != string::npos);
}

// Minimum derivation heights and lengths used by the generator
void testMinDerivations() {
    Grammar g = load(kExpr);
    SentenceGenerator gen(g, 1, 3);
    CHECK(gen.minLength("F") == 1);
    CHECK(gen.minLength("E") == 1);
    CHECK(gen.minHeight("F") == 1);
    CHECK(gen.minHeight("T") == 2);
    CHECK(gen.minHeight("E") == 3);
    CHECK(gen.canGenerate());
    for (int k = 0; k < 50; ++k) {
        vector<string> s = gen.sentence();
        FirstFollow ff(g);
        ParsingTable t = buildParsingTable(g, ff, buildSLRMachine(g));
        string joined;
        for (size_t i = 0; i < s.size(); ++i) joined += s[i] + " ";
        CHECK(Parser(g, t).parse(tokens(joined), false).accepted);
    }
    Grammar dead = load("S -> S a\n");
    SentenceGenerator none(dead, 1, 3);
    CHECK(!none.canGenerate());
    ShortestYield y(g);
    vector<string> e(1, "E");
    CHECK(y.expand(e) == vector<string>(1, "id"));
}

// Grammar loader directives and errors
void testGrammarLoader() {
    Grammar g = load("%left + -\n%right UMINUS\nE -> E + E | - E %prec UMINUS | id\n");
    CHECK(g.precedenceOf("+") == 1 && g.precedenceOf("UMINUS") == 2);
    CHECK(g.assocOf("-") == Assoc::LEFT);
    CHECK(g.productionPrecedence(2) == 2);
    CHECK(!g.isTerminal("UMINUS"));
    Grammar pipes = load("B -> B || A | A\nA -> x\n");
    CHECK(pipes.isTerminal("||") && pipes.productions().size() == 4);
    Grammar bad;
    string err;
    CHECK(!bad.loadFromString("%left +\n%left +\nE -> id\n", "u", ".", err));
    CHECK(err.find("declared twice") != string::npos);
}

}

int main() {
    testFirstFollow();
    testClosureGoto();
    testDigraph();
    testRegex();
    testPrecedence();
    testCompression();
    testWitness();
    testMinDerivations();
    testGrammarLoader();
    cout << "Unit tests: " << (checks - failures) << " of " << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
